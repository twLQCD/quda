#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <quda.h>
#include "test_util.h"
#include "gauge_field.h"
#include "fat_force_quda.h"
#include "misc.h"
#include "hisq_force_reference.h"
#include "hisq_force_quda.h"
#include "hw_quda.h"
#include <sys/time.h>
#include <dslash_quda.h>

using namespace quda;
extern void usage(char** argv);
cudaGaugeField *cudaFatLink = NULL;
cpuGaugeField  *cpuFatLink  = NULL;

cudaGaugeField *cudaOprod = NULL;
cpuGaugeField  *cpuOprod = NULL;

cudaGaugeField *cudaResult = NULL;
cpuGaugeField *cpuResult = NULL;


cpuGaugeField *cpuReference = NULL;

static QudaGaugeParam gaugeParam;


int verify_results = 1;
double accuracy = 1e-5;
int ODD_BIT = 1;
extern int device;
extern int xdim, ydim, zdim, tdim;
extern int gridsize_from_cmdline[];

extern QudaReconstructType link_recon;
extern QudaPrecision prec;
QudaPrecision link_prec = QUDA_SINGLE_PRECISION;
QudaPrecision hw_prec = QUDA_SINGLE_PRECISION;
QudaPrecision cpu_hw_prec = QUDA_SINGLE_PRECISION;
QudaPrecision mom_prec = QUDA_SINGLE_PRECISION;

void setPrecision(QudaPrecision precision)
{
  link_prec   = precision;
  return;
}




// Create a field of links that are not su3_matrices
void createNoisyLinkCPU(void** field, QudaPrecision prec, int seed)
{
  createSiteLinkCPU(field, prec, 0);

  srand(seed);
  for(int dir=0; dir<4; ++dir){
    for(int i=0; i<V*18; ++i){
      if(prec == QUDA_DOUBLE_PRECISION){
       double* ptr = ((double**)field)[dir] + i; 
       *ptr += (rand() - RAND_MAX/2.0)/(20.0*RAND_MAX);
      }else if(prec == QUDA_SINGLE_PRECISION){
     	  float* ptr = ((float**)field)[dir]+i;
        *ptr += (rand() - RAND_MAX/2.0)/(20.0*RAND_MAX);
      }  
    }
  }
  return;
}



// allocate memory
// set the layout, etc.
static void
hisq_force_init()
{
  initQuda(device);

  gaugeParam.X[0] = xdim;
  gaugeParam.X[1] = ydim;
  gaugeParam.X[2] = zdim;
  gaugeParam.X[3] = tdim;

  setDims(gaugeParam.X);

  gaugeParam.cpu_prec = link_prec;
  gaugeParam.cuda_prec = link_prec;
  gaugeParam.reconstruct = link_recon;
  gaugeParam.gauge_order = QUDA_QDP_GAUGE_ORDER;
  GaugeFieldParam gParam(0, gaugeParam);
  gParam.create = QUDA_ZERO_FIELD_CREATE;
  gParam.link_type = QUDA_ASQTAD_MOM_LINKS;
  gParam.anisotropy = 1;
  
  cpuFatLink = new cpuGaugeField(gParam);
  cpuOprod   = new cpuGaugeField(gParam);
  cpuResult  = new cpuGaugeField(gParam); 
 
  // create "gauge fields"
  int seed=0;
#ifdef MULTI_GPU
  seed += comm_rank();
#endif

  createNoisyLinkCPU((void**)cpuFatLink->Gauge_p(), gaugeParam.cpu_prec, seed);
  createNoisyLinkCPU((void**)cpuOprod->Gauge_p(), gaugeParam.cpu_prec, seed+1);
 
  cudaFatLink = new cudaGaugeField(gParam);
  cudaOprod   = new cudaGaugeField(gParam); 
  cudaResult  = new cudaGaugeField(gParam);

  cudaFatLink->loadCPUField(*cpuFatLink, QUDA_CPU_FIELD_LOCATION);
  cudaOprod->loadCPUField(*cpuOprod, QUDA_CPU_FIELD_LOCATION);


  cpuReference = new cpuGaugeField(gParam);
  return;
}


static void 
hisq_force_end()
{
  delete cpuFatLink;
  delete cpuOprod;
  delete cpuResult;

  delete cudaFatLink;
  delete cudaOprod;
  delete cudaResult;

  delete cpuReference;

  endQuda();
  return;
}

static void 
hisq_force_test()
{
  hisq_force_init();
  fermion_force::hisqForceInitCuda(&gaugeParam);

#define QUDA_VER ((10000*QUDA_VERSION_MAJOR) + (100*QUDA_VERSION_MINOR) + QUDA_VERSION_SUBMINOR)
#if (QUDA_VER > 400)
  initLatticeConstants(*cudaFatLink);
#else
  initCommonConstants(*cudaFatLink);
#endif
  initGaugeConstants(*cudaFatLink);


  double unitarize_eps = 1e-5;
  const double hisq_force_filter = 5e-5;
  const double max_det_error = 1e-12;
  const bool allow_svd = true;
  const bool svd_only = false;
  const double svd_rel_err = 1e-8;
  const double svd_abs_err = 1e-8;

  fermion_force::setUnitarizeForceConstants(unitarize_eps, hisq_force_filter, max_det_error, allow_svd, svd_only, svd_rel_err, svd_abs_err);



  int* num_failures_dev;
  if(cudaMalloc(&num_failures_dev, sizeof(int)) != cudaSuccess){
    errorQuda("cudaMalloc failed for num_failures_dev\n");
  }
  cudaMemset(num_failures_dev, 0, sizeof(int));

  printfQuda("Calling unitarizeForceCuda\n");
  fermion_force::unitarizeForceCuda(gaugeParam, *cudaOprod, *cudaFatLink, cudaResult, num_failures_dev);


  if(verify_results){
	  printfQuda("Calling unitarizeForceCPU\n");
    fermion_force::unitarizeForceCPU(gaugeParam, *cpuOprod, *cpuFatLink, cpuResult);
  }
  cudaResult->saveCPUField(*cpuReference, QUDA_CPU_FIELD_LOCATION);

  if(verify_results){
	  printfQuda("Comparing CPU and GPU results\n");
    for(int dir=0; dir<4; ++dir){
      int res = compare_floats(((char**)cpuReference->Gauge_p())[dir], ((char**)cpuResult->Gauge_p())[dir], cpuReference->Volume()*gaugeSiteSize, accuracy, gaugeParam.cpu_prec);
#ifdef MULTI_GPU
      comm_allreduce_int(&res);
      res /= comm_size();
#endif
      printfQuda("Dir:%d  Test %s\n",dir,(1 == res) ? "PASSED" : "FAILED");
    }

  }

  hisq_force_end();
}


static void
display_test_info()
{
  printfQuda("running the following fermion force computation test:\n");
    
  printfQuda("link_precision           link_reconstruct           space_dim(x/y/z)         T_dimension\n");
  printfQuda("%s                       %s                         %d/%d/%d                  %d \n", 
	 get_prec_str(link_prec),
	 get_recon_str(link_recon), 
	 xdim, ydim, zdim, tdim);
  return ;
    
}

void
usage_extra(char** argv )
{
  printf("Extra options: \n");
  printf("    --no_verify                                  # Do not verify the GPU results using CPU results\n");
  return ;
}

int 
main(int argc, char **argv) 
{
  int i;
  for (i =1;i < argc; i++){
	
    if(process_command_line_option(argc, argv, &i) == 0){
      continue;
    }  

    if( strcmp(argv[i], "--no_verify") == 0){
      verify_results=0;
      continue;	    
    }	


    fprintf(stderr, "ERROR: Invalid option:%s\n", argv[i]);
    usage(argv);
  }

#ifdef MULTI_GPU
    initCommsQuda(argc, argv, gridsize_from_cmdline, 4);
#endif

  setPrecision(prec);

  display_test_info();
    
  hisq_force_test();


#ifdef MULTI_GPU
    endCommsQuda();
#endif
    
    
  return EXIT_SUCCESS;
}
