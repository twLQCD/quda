#ifndef _COLOR_SPINOR_FIELD_H
#define _COLOR_SPINOR_FIELD_H

#include <quda_internal.h>
#include <quda.h>

#include <iostream>

#include <lattice_field.h>
#include <random_quda.h>

namespace quda {

  enum MemoryLocation { Device, Host, Remote };

  struct FullClover;

  /** Typedef for a set of spinors. Can be further divided into subsets ,e.g., with different precisions (not implemented currently) */
  typedef std::vector<ColorSpinorField*> CompositeColorSpinorField;

  /**
     Any spinor object can be qualified in the following categories:
     1. A regular spinor field (is_composite = false , is_component = false)
     2. A composite spinor field, i.e., a collection of spinor fields (is_composite = true , is_component = false)
     3. An individual component of a composite spinor field (is_composite = false , is_component = true)
     4. A subset of a composite spinor field (e.g., based on index range or field precision) : currently not implemented
  */
  struct CompositeColorSpinorFieldDescriptor {

     bool is_composite; //set to 'false' for a regular spinor field
     bool is_component; //set to 'true' if we want to work with an individual component (otherwise will work with the whole set)

     int  dim;//individual component has dim = 0
     int  id;

     int volume;       // volume of a single eigenvector
     int volumeCB;     // CB volume of a single eigenvector
     int stride;       // stride of a single eigenvector
     size_t real_length;  // physical length of a single eigenvector
     size_t length;       // length including pads (but not ghost zones)

     size_t bytes;      // size in bytes of spinor field
     size_t norm_bytes; // makes no sense but let's keep it...

     CompositeColorSpinorFieldDescriptor()
     : is_composite(false), is_component(false), dim(0), id(0), volume(0), volumeCB(0),
       stride(0), real_length(0), length(0), bytes(0), norm_bytes(0)  {};

     CompositeColorSpinorFieldDescriptor(bool is_composite, int dim, bool is_component = false, int id = 0)
     : is_composite(is_composite), is_component(is_component), dim(dim), id(id), volume(0), volumeCB(0),
       stride(0), real_length(0), length(0), bytes(0), norm_bytes(0)
     {
        if(is_composite && is_component) errorQuda("\nComposite type is not implemented.\n");
        else if(is_composite && dim == 0) is_composite = false;
     }

     CompositeColorSpinorFieldDescriptor(const CompositeColorSpinorFieldDescriptor &descr)
     {
       is_composite = descr.is_composite;
       is_component = descr.is_component;

       if(is_composite && is_component) errorQuda("\nComposite type is not implemented.\n");

       dim = descr.dim;
       id  = descr.id;

       volume   = descr.volume;
       volumeCB = descr.volumeCB;
       stride   = descr.stride;       // stride of a single eigenvector
       real_length = descr.real_length;  // physical length of a single eigenvector
       length      = descr.length;       // length including pads (but not ghost zones)

       bytes = descr.bytes;      // size in bytes of spinor field
       norm_bytes = descr.norm_bytes; // makes no sense but let's keep it...
     }

  };

  class ColorSpinorParam : public LatticeFieldParam {

  public:
    QudaFieldLocation location; // where are we storing the field (CUDA or CPU)?

    int nColor; // Number of colors of the field
    int nSpin; // =1 for staggered, =2 for coarse Dslash, =4 for 4d spinor

    QudaTwistFlavorType twistFlavor; // used by twisted mass

    QudaSiteOrder siteOrder; // defined for full fields

    QudaFieldOrder fieldOrder; // Float, Float2, Float4 etc.
    QudaGammaBasis gammaBasis;
    QudaFieldCreate create; //

    QudaDWFPCType PCtype; // used to select preconditioning method in DWF

    void *v; // pointer to field
    void *norm;

    //! for deflation solvers:
    bool is_composite;
    int composite_dim;    //e.g., number of eigenvectors in the set
    bool is_component;
    int component_id;          //eigenvector index

    ColorSpinorParam(const ColorSpinorField &a);

  ColorSpinorParam()
    : LatticeFieldParam(), location(QUDA_INVALID_FIELD_LOCATION), nColor(0),
      nSpin(0), twistFlavor(QUDA_TWIST_INVALID), siteOrder(QUDA_INVALID_SITE_ORDER),
      fieldOrder(QUDA_INVALID_FIELD_ORDER), gammaBasis(QUDA_INVALID_GAMMA_BASIS),
      create(QUDA_INVALID_FIELD_CREATE), PCtype(QUDA_PC_INVALID),
      is_composite(false), composite_dim(0), is_component(false), component_id(0) { ; }

      // used to create cpu params
  ColorSpinorParam(void *V, QudaInvertParam &inv_param, const int *X, const bool pc_solution,
		   QudaFieldLocation location=QUDA_CPU_FIELD_LOCATION)
    : LatticeFieldParam(4, X, 0, inv_param.cpu_prec), location(location), nColor(3),
      nSpin( (inv_param.dslash_type == QUDA_ASQTAD_DSLASH ||
              inv_param.dslash_type == QUDA_STAGGERED_DSLASH ||
	      inv_param.dslash_type == QUDA_LAPLACE_DSLASH) ? 1 : 4),
      twistFlavor(inv_param.twist_flavor), siteOrder(QUDA_INVALID_SITE_ORDER),
      fieldOrder(QUDA_INVALID_FIELD_ORDER), gammaBasis(inv_param.gamma_basis),
      create(QUDA_REFERENCE_FIELD_CREATE),
      PCtype(((inv_param.dslash_type==QUDA_DOMAIN_WALL_4D_DSLASH)||
	      (inv_param.dslash_type==QUDA_MOBIUS_DWF_DSLASH))?QUDA_4D_PC:QUDA_5D_PC ),
      v(V), is_composite(false), composite_dim(0), is_component(false), component_id(0) {

        if (nDim > QUDA_MAX_DIM) errorQuda("Number of dimensions too great");
	for (int d=0; d<nDim; d++) x[d] = X[d];

	if (!pc_solution) {
	  siteSubset = QUDA_FULL_SITE_SUBSET;;
	} else {
	  x[0] /= 2; // X defined the full lattice dimensions
	  siteSubset = QUDA_PARITY_SITE_SUBSET;
	}

	if (inv_param.dslash_type == QUDA_DOMAIN_WALL_DSLASH ||
	    inv_param.dslash_type == QUDA_DOMAIN_WALL_4D_DSLASH ||
	    inv_param.dslash_type == QUDA_MOBIUS_DWF_DSLASH) {
	  nDim++;
	  x[4] = inv_param.Ls;
	} else if (inv_param.dslash_type == QUDA_TWISTED_MASS_DSLASH && (twistFlavor == QUDA_TWIST_NONDEG_DOUBLET)) {
	  nDim++;
	  x[4] = 2;//for two flavors
	} else if (inv_param.dslash_type == QUDA_STAGGERED_DSLASH || inv_param.dslash_type == QUDA_ASQTAD_DSLASH) {
	  nDim++;
	  x[4] = inv_param.Ls;
	}

	if (inv_param.dirac_order == QUDA_INTERNAL_DIRAC_ORDER) {
	  fieldOrder = (precision == QUDA_DOUBLE_PRECISION || nSpin == 1) ?
	    QUDA_FLOAT2_FIELD_ORDER : QUDA_FLOAT4_FIELD_ORDER;
	  siteOrder = QUDA_EVEN_ODD_SITE_ORDER;
	} else if (inv_param.dirac_order == QUDA_CPS_WILSON_DIRAC_ORDER) {
	  fieldOrder = QUDA_SPACE_SPIN_COLOR_FIELD_ORDER;
	  siteOrder = QUDA_ODD_EVEN_SITE_ORDER;
	} else if (inv_param.dirac_order == QUDA_QDP_DIRAC_ORDER) {
	  fieldOrder = QUDA_SPACE_COLOR_SPIN_FIELD_ORDER;
	  siteOrder = QUDA_EVEN_ODD_SITE_ORDER;
	} else if (inv_param.dirac_order == QUDA_DIRAC_ORDER) {
	  fieldOrder = QUDA_SPACE_SPIN_COLOR_FIELD_ORDER;
	  siteOrder = QUDA_EVEN_ODD_SITE_ORDER;
	} else if (inv_param.dirac_order == QUDA_QDPJIT_DIRAC_ORDER) {
	  fieldOrder = QUDA_QDPJIT_FIELD_ORDER;
	  siteOrder = QUDA_EVEN_ODD_SITE_ORDER;
	} else if (inv_param.dirac_order == QUDA_TIFR_PADDED_DIRAC_ORDER) {
	  fieldOrder = QUDA_PADDED_SPACE_SPIN_COLOR_FIELD_ORDER;
	  siteOrder = QUDA_EVEN_ODD_SITE_ORDER;
	} else {
	  errorQuda("Dirac order %d not supported", inv_param.dirac_order);
	}
      }

    // normally used to create cuda param from a cpu param
  ColorSpinorParam(ColorSpinorParam &cpuParam, QudaInvertParam &inv_param,
		   QudaFieldLocation location=QUDA_CUDA_FIELD_LOCATION)
    : LatticeFieldParam(cpuParam.nDim, cpuParam.x, inv_param.sp_pad, inv_param.cuda_prec),
      location(location), nColor(cpuParam.nColor), nSpin(cpuParam.nSpin), twistFlavor(cpuParam.twistFlavor),
      siteOrder(QUDA_EVEN_ODD_SITE_ORDER), fieldOrder(QUDA_INVALID_FIELD_ORDER),
      gammaBasis(nSpin == 4? QUDA_UKQCD_GAMMA_BASIS : QUDA_DEGRAND_ROSSI_GAMMA_BASIS),
      create(QUDA_COPY_FIELD_CREATE), PCtype(cpuParam.PCtype), v(0), is_composite(cpuParam.is_composite), composite_dim(cpuParam.composite_dim), is_component(false), component_id(0)
      {
	siteSubset = cpuParam.siteSubset;
	fieldOrder = (precision == QUDA_DOUBLE_PRECISION || nSpin == 1) ?
	  QUDA_FLOAT2_FIELD_ORDER : QUDA_FLOAT4_FIELD_ORDER;
      }

    /**
       If using CUDA native fields, this function will ensure that the
       field ordering is appropriate for the new precision setting to
       maintain this status
       @param precision New precision value
     */
    void setPrecision(QudaPrecision precision) {
      // is the current status in native field order?
      bool native = false;
      if ( ((this->precision == QUDA_DOUBLE_PRECISION || nSpin==1 || nSpin==2) &&
	    (fieldOrder == QUDA_FLOAT2_FIELD_ORDER)) ||
	   ((this->precision == QUDA_SINGLE_PRECISION || this->precision == QUDA_HALF_PRECISION) &&
	    (nSpin==4) && fieldOrder == QUDA_FLOAT4_FIELD_ORDER) ) { native = true; }

      this->precision = precision;

      // if this is a native field order, let's preserve that status, else keep the same field order
      if (native) fieldOrder = (precision == QUDA_DOUBLE_PRECISION || nSpin == 1 || nSpin == 2) ?
	QUDA_FLOAT2_FIELD_ORDER : QUDA_FLOAT4_FIELD_ORDER;
    }

    void print() {
      printfQuda("nColor = %d\n", nColor);
      printfQuda("nSpin = %d\n", nSpin);
      printfQuda("twistFlavor = %d\n", twistFlavor);
      printfQuda("nDim = %d\n", nDim);
      for (int d=0; d<nDim; d++) printfQuda("x[%d] = %d\n", d, x[d]);
      printfQuda("precision = %d\n", precision);
      printfQuda("pad = %d\n", pad);
      printfQuda("siteSubset = %d\n", siteSubset);
      printfQuda("siteOrder = %d\n", siteOrder);
      printfQuda("fieldOrder = %d\n", fieldOrder);
      printfQuda("gammaBasis = %d\n", gammaBasis);
      printfQuda("create = %d\n", create);
      printfQuda("v = %lx\n", (unsigned long)v);
      printfQuda("norm = %lx\n", (unsigned long)norm);
      //! for deflation etc.
      if(is_composite) printfQuda("Number of elements = %d\n", composite_dim);
    }

    virtual ~ColorSpinorParam() {
    }

  };

  class cpuColorSpinorField;
  class cudaColorSpinorField;

  class ColorSpinorField : public LatticeField {

  private:
    void create(int nDim, const int *x, int Nc, int Ns, QudaTwistFlavorType Twistflavor,
		QudaPrecision precision, int pad, QudaSiteSubset subset,
		QudaSiteOrder siteOrder, QudaFieldOrder fieldOrder, QudaGammaBasis gammaBasis,
		QudaDWFPCType PCtype);
    void destroy();

  protected:
    bool init;

    int nColor;
    int nSpin;

    int nDim;
    int x[QUDA_MAX_DIM];

    int volume;
    int volumeCB;
    int pad;
    int stride;

    QudaTwistFlavorType twistFlavor;

    QudaDWFPCType PCtype; // used to select preconditioning method in DWF

    size_t real_length; // physical length only
    size_t length; // length including pads, but not ghost zone - used for BLAS

    void *v; // the field elements
    void *norm; // the normalization field

    void *v_h; // the field elements
    void *norm_h; // the normalization field

    // multi-GPU parameters

    void* ghost[2][QUDA_MAX_DIM]; // pointers to the ghost regions - NULL by default
    void* ghostNorm[2][QUDA_MAX_DIM]; // pointers to ghost norms - NULL by default

    mutable int ghostFace[QUDA_MAX_DIM];// the size of each face
    mutable int ghostOffset[QUDA_MAX_DIM][2]; // offsets to each ghost zone
    mutable int ghostNormOffset[QUDA_MAX_DIM][2]; // offsets to each ghost zone for norm field

    mutable size_t ghost_length; // length of ghost zone
    mutable size_t ghost_norm_length; // length of ghost zone for norm

    mutable void *ghost_buf[2*QUDA_MAX_DIM]; // wrapper that points to current ghost zone

    size_t bytes; // size in bytes of spinor field
    size_t norm_bytes; // size in bytes of norm field
    mutable size_t ghost_bytes; // size in bytes of the ghost field
    mutable size_t ghost_face_bytes[QUDA_MAX_DIM];

    QudaSiteSubset siteSubset;
    QudaSiteOrder siteOrder;
    QudaFieldOrder fieldOrder;
    QudaGammaBasis gammaBasis;

    // in the case of full fields, these are references to the even / odd sublattices
    ColorSpinorField *even;
    ColorSpinorField *odd;

    //! used for deflation eigenvector sets etc.:
    CompositeColorSpinorFieldDescriptor composite_descr;//containes info about the set
    //
    CompositeColorSpinorField components;

    void createGhostZone(int nFace, bool spin_project=true) const;

    // resets the above attributes based on contents of param
    void reset(const ColorSpinorParam &);
    void fill(ColorSpinorParam &) const;
    static void checkField(const ColorSpinorField &, const ColorSpinorField &);

    char aux_string[TuneKey::aux_n]; // used as a label in the autotuner
    void setTuningString(); // set the vol_string and aux_string for use in tuning

  public:
    //ColorSpinorField();
    ColorSpinorField(const ColorSpinorField &);
    ColorSpinorField(const ColorSpinorParam &);

    virtual ~ColorSpinorField();

    virtual ColorSpinorField& operator=(const ColorSpinorField &);

    int Ncolor() const { return nColor; }
    int Nspin() const { return nSpin; }
    QudaTwistFlavorType TwistFlavor() const { return twistFlavor; }
    int Ndim() const { return nDim; }
    const int* X() const { return x; }
    int X(int d) const { return x[d]; }
    size_t RealLength() const { return real_length; }
    size_t Length() const { return length; }
    int Stride() const { return stride; }
    int Volume() const { return volume; }
    int VolumeCB() const { return siteSubset == QUDA_PARITY_SITE_SUBSET ? volume : volume / 2; }
    int Pad() const { return pad; }
    size_t Bytes() const { return bytes; }
    size_t NormBytes() const { return norm_bytes; }
    size_t GhostBytes() const { return ghost_bytes; }
    size_t GhostNormBytes() const { return ghost_bytes; }
    void PrintDims() const { printfQuda("dimensions=%d %d %d %d\n", x[0], x[1], x[2], x[3]); }

    inline const char *AuxString() const { return aux_string; }

    void* V() {return v;}
    const void* V() const {return v;}
    void* Norm(){return norm;}
    const void* Norm() const {return norm;}
    virtual const void* Ghost2() const { return nullptr; }

    /**
       Do the exchange between neighbouring nodes of the data in
       sendbuf storing the result in recvbuf.  The arrays are ordered
       (2*dim + dir).
       @param recvbuf Packed buffer where we store the result
       @param sendbuf Packed buffer from which we're sending
       @param nFace Number of layers we are exchanging
     */
    void exchange(void **ghost, void **sendbuf, int nFace=1) const;

    /**
       This is a unified ghost exchange function for doing a complete
       halo exchange regardless of the type of field.  All dimensions
       are exchanged and no spin projection is done in the case of
       Wilson fermions.
       @param[in] parity Field parity
       @param[in] nFace Depth of halo exchange
       @param[in] dagger Is this for a dagger operator (only relevant for spin projected Wilson)
       @param[in] pack_destination Destination of the packing buffer
       @param[in] halo_location Destination of the halo reading buffer
       @param[in] gdr_send Are we using GDR for sending
       @param[in] gdr_recv Are we using GDR for receiving
     */
    virtual void exchangeGhost(QudaParity parity, int nFace, int dagger, const MemoryLocation *pack_destination=nullptr,
			       const MemoryLocation *halo_location=nullptr, bool gdr_send=false, bool gdr_recv=false) const = 0;

    /**
      This function returns true if the field is stored in an internal
      field order, given the precision and the length of the spin
      dimension.
      */
    bool isNative() const;

    bool IsComposite() const { return composite_descr.is_composite; }
    bool IsComponent() const { return composite_descr.is_component; }

    int CompositeDim() const { return composite_descr.dim; }
    int ComponentId() const { return composite_descr.id; }
    int ComponentVolume() const { return composite_descr.volume; }
    int ComponentVolumeCB() const { return composite_descr.volumeCB; }
    int ComponentStride() const { return composite_descr.stride; }
    size_t ComponentLength() const { return composite_descr.length; }
    size_t ComponentRealLength() const { return composite_descr.real_length; }

    size_t ComponentBytes() const { return composite_descr.bytes; }
    size_t ComponentNormBytes() const { return composite_descr.norm_bytes; }

    QudaDWFPCType DWFPCtype() const { return PCtype; }

    QudaSiteSubset SiteSubset() const { return siteSubset; }
    QudaSiteOrder SiteOrder() const { return siteOrder; }
    QudaFieldOrder FieldOrder() const { return fieldOrder; }
    QudaGammaBasis GammaBasis() const { return gammaBasis; }

    size_t GhostLength() const { return ghost_length; }
    const int *GhostFace() const { return ghostFace; }
    int GhostOffset(const int i) const { return ghostOffset[i][0]; }
    int GhostOffset(const int i, const int j) const { return ghostOffset[i][j]; }
    int GhostNormOffset(const int i ) const { return ghostNormOffset[i][0]; }
    int GhostNormOffset(const int i, const int j) const { return ghostNormOffset[i][j]; }

    void* Ghost(const int i);
    const void* Ghost(const int i) const;
    void* GhostNorm(const int i);
    const void* GhostNorm(const int i) const;

    /**
       Return array of pointers to the ghost zones (ordering dim*2+dir)
     */
    void* const* Ghost() const;

    const ColorSpinorField& Even() const;
    const ColorSpinorField& Odd() const;

    ColorSpinorField& Even();
    ColorSpinorField& Odd();

    ColorSpinorField& Component(const int idx) const;
    ColorSpinorField& Component(const int idx);

    CompositeColorSpinorField& Components(){
      return components;
    };

    virtual void Source(const QudaSourceType sourceType, const int st=0, const int s=0, const int c=0) = 0;

    virtual void PrintVector(unsigned int x) = 0;

    /**
     * Compute the n-dimensional site index given the 1-d offset index
     * @param y n-dimensional site index
     * @param i 1-dimensional site index
     */
    void LatticeIndex(int *y, int i) const;

    /**
     * Compute the 1-d offset index given the n-dimensional site index
     * @param i 1-dimensional site index
     * @param y n-dimensional site index
     */
    void OffsetIndex(int &i, int *y) const;

    static ColorSpinorField* Create(const ColorSpinorParam &param);
    static ColorSpinorField* Create(const ColorSpinorField &src, const ColorSpinorParam &param);
    ColorSpinorField* CreateCoarse(const int *geoblockSize, int spinBlockSize, int Nvec,
				   QudaFieldLocation location=QUDA_INVALID_FIELD_LOCATION);
    ColorSpinorField* CreateFine(const int *geoblockSize, int spinBlockSize, int Nvec,
				 QudaFieldLocation location=QUDA_INVALID_FIELD_LOCATION);

    friend std::ostream& operator<<(std::ostream &out, const ColorSpinorField &);
    friend class ColorSpinorParam;
  };

  // CUDA implementation
  class cudaColorSpinorField : public ColorSpinorField {

    friend class cpuColorSpinorField;

  private:
    bool alloc; // whether we allocated memory
    bool init;

    bool texInit; // whether a texture object has been created or not
    mutable bool ghostTexInit; // whether the ghost texture object has been created
#ifdef USE_TEXTURE_OBJECTS
    cudaTextureObject_t tex;
    cudaTextureObject_t texNorm;
    void createTexObject();
    void destroyTexObject();
    mutable cudaTextureObject_t ghostTex[4]; // these are double buffered and variants to host-mapped buffers
    mutable cudaTextureObject_t ghostTexNorm[4];
    void createGhostTexObject() const;
    void destroyGhostTexObject() const;
#endif

    bool reference; // whether the field is a reference or not

    static size_t ghostFaceBytes;
    static bool initGhostFaceBuffer;

    mutable void *ghost_field_tex[4]; // instance pointer to GPU halo buffer (used to check if static allocation has changed)

    void create(const QudaFieldCreate);
    void destroy();
    void copy(const cudaColorSpinorField &);

    void zeroPad();

    /**
      This function is responsible for calling the correct copy kernel
      given the nature of the source field and the desired destination.
      */
    void copySpinorField(const ColorSpinorField &src);

    void loadSpinorField(const ColorSpinorField &src);
    void saveSpinorField (ColorSpinorField &src) const;

    /** Keep track of which pinned-memory buffer we used for creating message handlers */
    size_t bufferMessageHandler;

  public:

    //cudaColorSpinorField();
    cudaColorSpinorField(const cudaColorSpinorField&);
    cudaColorSpinorField(const ColorSpinorField&, const ColorSpinorParam&);
    cudaColorSpinorField(const ColorSpinorField&);
    cudaColorSpinorField(const ColorSpinorParam&);
    virtual ~cudaColorSpinorField();

    ColorSpinorField& operator=(const ColorSpinorField &);
    cudaColorSpinorField& operator=(const cudaColorSpinorField&);
    cudaColorSpinorField& operator=(const cpuColorSpinorField&);

    void switchBufferPinned();

    /**
       @brief Create the communication handlers and buffers
       @param[in] nFace Depth of each halo
       @param[in] spin_project Whether the halos are spin projected (Wilson-type fermions only)
    */
    void createComms(int nFace, bool spin_project=true);

    /**
       @brief Destroy the communication handlers and buffers
    */
    void destroyComms();

    /**
       @brief Allocate the ghost buffers
       @param[in] nFace Depth of each halo
       @param[in] spin_project Whether the halos are spin projected (Wilson-type fermions only)
    */
    void allocateGhostBuffer(int nFace, bool spin_project=true) const;

    /**
       @brief Free statically allocated ghost buffers
    */
    static void freeGhostBuffer(void);

    /**
       @brief Packs the cudaColorSpinorField's ghost zone
       @param[in] nFace How many faces to pack (depth)
       @param[in] parity Parity of the field
       @param[in] dim Labels space-time dimensions
       @param[in] dir Pack data to send in forward of backward directions, or both
       @param[in] dagger Whether the operator is the Hermitian conjugate or not
       @param[in] stream Which stream to use for the kernel
       @param[out] buffer Optional parameter where the ghost should be
       stored (default is to use cudaColorSpinorField::ghostFaceBuffer)
       @param[in] location Are we packing directly into local device memory, zero-copy memory or remote memory
       @param[in] a Twisted mass parameter (default=0)
       @param[in] b Twisted mass parameter (default=0)
      */
    void packGhost(const int nFace, const QudaParity parity, const int dim, const QudaDirection dir, const int dagger,
		   cudaStream_t* stream, MemoryLocation location[2*QUDA_MAX_DIM], double a=0, double b=0);


    void packGhostExtended(const int nFace, const int R[], const QudaParity parity, const int dim, const QudaDirection dir,
			   const int dagger,cudaStream_t* stream, bool zero_copy=false);


    void packGhost(FullClover &clov, FullClover &clovInv, const int nFace, const QudaParity parity, const int dim,
		   const QudaDirection dir, const int dagger, cudaStream_t* stream, void *buffer=0, double a=0);

    /**
      Initiate the gpu to cpu send of the ghost zone (halo)
      @param ghost_spinor Where to send the ghost zone
      @param nFace Number of face to send
      @param dim The lattice dimension we are sending
      @param dir The direction (QUDA_BACKWARDS or QUDA_FORWARDS)
      @param dagger Whether the operator is daggerer or not
      @param stream The array of streams to use
      */
    void sendGhost(void *ghost_spinor, const int nFace, const int dim, const QudaDirection dir,
        const int dagger, cudaStream_t *stream);

    /**
      Initiate the cpu to gpu send of the ghost zone (halo)
      @param ghost_spinor Source of the ghost zone
      @param nFace Number of face to send
      @param dim The lattice dimension we are sending
      @param dir The direction (QUDA_BACKWARDS or QUDA_FORWARDS)
      @param dagger Whether the operator is daggerer or not
      @param stream The array of streams to use
      */
    void unpackGhost(const void* ghost_spinor, const int nFace, const int dim,
        const QudaDirection dir, const int dagger, cudaStream_t* stream);

    /**
      Initiate the cpu to gpu copy of the extended border region
      @param ghost_spinor Source of the ghost zone
      @param parity Parity of the field
      @param nFace Number of face to send
      @param dim The lattice dimension we are sending
      @param dir The direction (QUDA_BACKWARDS or QUDA_FORWARDS)
      @param dagger Whether the operator is daggered or not
      @param stream The array of streams to use
      @param zero_copy Whether we are unpacking from zero_copy memory
      */
    void unpackGhostExtended(const void* ghost_spinor, const int nFace, const QudaParity parity,
			     const int dim, const QudaDirection dir, const int dagger, cudaStream_t* stream, bool zero_copy);


    void streamInit(cudaStream_t *stream_p);

    void pack(int nFace, int parity, int dagger, int stream_idx,
	      MemoryLocation location[], double a=0, double b=0);

    void packExtended(const int nFace, const int R[], const int parity, const int dagger,
        const int dim,  cudaStream_t *stream_p, const bool zeroCopyPack=false);

    void gather(int nFace, int dagger, int dir, cudaStream_t *stream_p=NULL);

    void recvStart(int nFace, int dir, int dagger=0, cudaStream_t *stream_p=NULL, bool gdr=false);
    void sendStart(int nFace, int dir, int dagger=0, cudaStream_t *stream_p=NULL, bool gdr=false);
    void commsStart(int nFace, int dir, int dagger=0, cudaStream_t *stream_p=NULL, bool gdr=false);
    int commsQuery(int nFace, int dir, int dagger=0, cudaStream_t *stream_p=NULL, bool gdr=false);
    void commsWait(int nFace, int dir, int dagger=0, cudaStream_t *stream_p=NULL, bool gdr=false);

    void scatter(int nFace, int dagger, int dir, cudaStream_t *stream_p);
    void scatter(int nFace, int dagger, int dir);

    void scatterExtended(int nFace, int parity, int dagger, int dir);

    const void* Ghost2() const { return ghost_field_tex[bufferIndex]; }

    /**
       @brief This is a unified ghost exchange function for doing a complete
       halo exchange regardless of the type of field.  All dimensions
       are exchanged and no spin projection is done in the case of
       Wilson fermions.
       @param[in] parity Field parity
       @param[in] nFace Depth of halo exchange
       @param[in] dagger Is this for a dagger operator (only relevant for spin projected Wilson)
       @param[in] pack_destination Destination of the packing buffer
       @param[in] halo_location Destination of the halo reading buffer
       @param[in] gdr_send Are we using GDR for sending
       @param[in] gdr_recv Are we using GDR for receiving
     */
    void exchangeGhost(QudaParity parity, int nFace, int dagger, const MemoryLocation *pack_destination=nullptr,
		       const MemoryLocation *halo_location=nullptr, bool gdr_send=false, bool gdr_recv=false) const;

#ifdef USE_TEXTURE_OBJECTS
    const cudaTextureObject_t& Tex() const { return tex; }
    const cudaTextureObject_t& TexNorm() const { return texNorm; }
    const cudaTextureObject_t& GhostTex() const { return ghostTex[bufferIndex]; }
    const cudaTextureObject_t& GhostTexNorm() const { return ghostTexNorm[bufferIndex]; }
#endif

    cudaColorSpinorField& Component(const int idx) const;
    CompositeColorSpinorField& Components() const;
    void CopySubset(cudaColorSpinorField& dst, const int range, const int first_element=0) const;

    void zero();

    friend std::ostream& operator<<(std::ostream &out, const cudaColorSpinorField &);

    void getTexObjectInfo() const;

    void Source(const QudaSourceType sourceType, const int st=0, const int s=0, const int c=0);

    void PrintVector(unsigned int x);
  };

  // CPU implementation
  class cpuColorSpinorField : public ColorSpinorField {

    friend class cudaColorSpinorField;

  public:
    static void* fwdGhostFaceBuffer[QUDA_MAX_DIM]; //cpu memory
    static void* backGhostFaceBuffer[QUDA_MAX_DIM]; //cpu memory
    static void* fwdGhostFaceSendBuffer[QUDA_MAX_DIM]; //cpu memory
    static void* backGhostFaceSendBuffer[QUDA_MAX_DIM]; //cpu memory
    static int initGhostFaceBuffer;
    static size_t ghostFaceBytes[QUDA_MAX_DIM];

    private:
    //void *v; // the field elements
    //void *norm; // the normalization field
    bool init;
    bool reference; // whether the field is a reference or not

    void create(const QudaFieldCreate);
    void destroy();

    public:
    //cpuColorSpinorField();
    cpuColorSpinorField(const cpuColorSpinorField&);
    cpuColorSpinorField(const ColorSpinorField&);
    cpuColorSpinorField(const ColorSpinorField&, const ColorSpinorParam&);
    cpuColorSpinorField(const ColorSpinorParam&);
    virtual ~cpuColorSpinorField();

    ColorSpinorField& operator=(const ColorSpinorField &);
    cpuColorSpinorField& operator=(const cpuColorSpinorField&);
    cpuColorSpinorField& operator=(const cudaColorSpinorField&);

    void Source(const QudaSourceType sourceType, const int st=0, const int s=0, const int c=0);
    static int Compare(const cpuColorSpinorField &a, const cpuColorSpinorField &b, const int resolution=1);
    void PrintVector(unsigned int x);

    /**
       @brief Allocate the ghost buffers
       @param[in] nFace Depth of each halo
    */
    void allocateGhostBuffer(int nFace) const;
    static void freeGhostBuffer(void);

    void packGhost(void **ghost, const QudaParity parity, const int nFace, const int dagger) const;
    void unpackGhost(void* ghost_spinor, const int dim,
		     const QudaDirection dir, const int dagger);

    void copy(const cpuColorSpinorField&);
    void zero();

    /**
       @brieff This is a unified ghost exchange function for doing a complete
       halo exchange regardless of the type of field.  All dimensions
       are exchanged and no spin projection is done in the case of
       Wilson fermions.
       @param[in] parity Field parity
       @param[in] nFace Depth of halo exchange
       @param[in] dagger Is this for a dagger operator (only relevant for spin projected Wilson)
       @param[in] pack_destination Destination of the packing buffer
       @param[in] halo_location Destination of the halo reading buffer
       @param[in] gdr_send Dummy for CPU
       @param[in] gdr_recv Dummy for GPU
     */
    void exchangeGhost(QudaParity parity, int nFace, int dagger, const MemoryLocation *pack_destination=nullptr,
		       const MemoryLocation *halo_location=nullptr, bool gdr_send=false, bool gdr_recv=false) const;

  };

  void copyGenericColorSpinor(ColorSpinorField &dst, const ColorSpinorField &src,
      QudaFieldLocation location, void *Dst=0, void *Src=0,
      void *dstNorm=0, void*srcNorm=0);
  void genericSource(cpuColorSpinorField &a, QudaSourceType sourceType, int x, int s, int c);
  int genericCompare(const cpuColorSpinorField &a, const cpuColorSpinorField &b, int tol);
  void genericPrintVector(cpuColorSpinorField &a, unsigned int x);

  void wuppertalStep(ColorSpinorField &out, const ColorSpinorField &in, int parity, const GaugeField& U, double A, double B);
  void wuppertalStep(ColorSpinorField &out, const ColorSpinorField &in, int parity, const GaugeField& U, double alpha);

  void exchangeExtendedGhost(cudaColorSpinorField* spinor, int R[], int parity, cudaStream_t *stream_p);

  void copyExtendedColorSpinor(ColorSpinorField &dst, const ColorSpinorField &src,
      QudaFieldLocation location, const int parity, void *Dst, void *Src, void *dstNorm, void *srcNorm);

  /**
     @brief Generic ghost packing routine

     @param[out] ghost Array of packed ghosts with array ordering [2*dim+dir]
     @param[in] a Input field that is being packed
     @param[in] parity Which parity are we packing
     @param[in] dagger Is for a dagger operator (presently ignored)
     @param[in[ location Array specifiying the memory location of each resulting ghost [2*dim+dir]
  */
  void genericPackGhost(void **ghost, const ColorSpinorField &a, QudaParity parity,
			int nFace, int dagger, MemoryLocation *destination=nullptr);

  /*Generate a gaussian distributed spinor
   * @param src The spinorfield
   * @param seed Seed
   * */
  void spinorGauss(ColorSpinorField &src, int seed);

  /*Generate a gaussian distributed spinor
   * @param src The spinorfield
   * @param randstates Random state
   * */
  void spinorGauss(ColorSpinorField &src, RNG& randstates);

} // namespace quda

#endif // _COLOR_SPINOR_FIELD_H
