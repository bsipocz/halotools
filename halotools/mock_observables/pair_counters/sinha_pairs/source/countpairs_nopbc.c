#include <stdio.h>
#include <math.h>
#include <stdlib.h>

#include "gridlink.h"//function proto-type for gridlink
#include "countpairs.h" //function proto-type
#include "cellarray.h" //definition of struct cellarray
#include "utils.h" //all of the utilities

#ifdef USE_AVX
#include "avx_calls.h"
#else
#define PREFETCH(mem)        asm ("prefetcht0 %0"::"m"(mem))
#endif

#define BLOCK_SIZE     16


#ifdef USE_ISPC
#include "countpairs_ispc.h"
#endif

#ifdef USE_OMP
#include <omp.h>
#endif


void countpairs_nopbc(const int ND1, const double * const X1, const double * const Y1, const double  * const Z1,
				const int ND2, const double * const X2, const double * const Y2, const double  * const Z2,
				const double xmin, const double xmax,
				const double ymin, const double ymax,
				const double zmin, const double zmax,
				const int autocorr,
				const double rpmax,
#ifdef USE_OMP
				const int numthreads,
#endif
				const int nrpbin, const double * restrict rupp, int **paircounts)
{
  int bin_refine_factor=1;
  if(autocorr==1) {
    bin_refine_factor=2;
  } else {
    bin_refine_factor=2;
  }
#ifdef USE_OMP
	if(numthreads > 1) {

		//I have written it this way to maintain
		//compatibility with the previous chunk.
		// For instance, DR calculations might be faster
		// for bin_refine=2...
		if(autocorr==1) {
			bin_refine_factor=1;//benchmarked -> gives super-scalar performance
		} else {
			bin_refine_factor=1;//not benchmarked -> so may be changed in the future
		}
	}
#endif
	

  /*---Create 3-D lattice--------------------------------------*/
  int nmesh_x=0,nmesh_y=0,nmesh_z=0;
		
  cellarray *lattice1 = gridlink_nopbc(ND1, X1, Y1, Z1, xmin, xmax, ymin, ymax, zmin, zmax, rpmax, bin_refine_factor, bin_refine_factor, bin_refine_factor, &nmesh_x, &nmesh_y, &nmesh_z);
  cellarray *lattice2 = NULL;
  if(autocorr==0) {
	int ngrid2_x=0,ngrid2_y=0,ngrid2_z=0;
	lattice2 = gridlink_nopbc(ND2, X2, Y2, Z2, xmin, xmax, ymin, ymax, zmin, zmax, rpmax, bin_refine_factor, bin_refine_factor, bin_refine_factor, &ngrid2_x, &ngrid2_y, &ngrid2_z);
	assert(nmesh_x == ngrid2_x && "Both lattices have the same number of X bins");
	assert(nmesh_y == ngrid2_y && "Both lattices have the same number of Y bins");
	assert(nmesh_z == ngrid2_z && "Both lattices have the same number of Z bins");
  } else {
	  lattice2 = lattice1;
  }

#ifndef USE_OMP
	unsigned int npairs[nrpbin];	
	for(int i=0; i < nrpbin;i++) npairs[i] = 0;
#else
	omp_set_num_threads(numthreads);
	unsigned int **all_npairs = (unsigned int **) matrix_calloc(sizeof(unsigned int), numthreads, nrpbin);
#endif

  DOUBLE rupp_sqr[nrpbin];
  for(int i=0; i < nrpbin;i++) {
    rupp_sqr[i] = rupp[i]*rupp[i];
	}

#ifdef OUTPUT_RPAVG  
#ifndef USE_OMP	
  DOUBLE rpavg[nrpbin];
  for(int i=0; i < nrpbin;i++) {
    rpavg[i] = 0.0;
	}
#else	
	DOUBLE **all_rpavg = (DOUBLE **) matrix_calloc(sizeof(DOUBLE),numthreads,nrpbin);
#endif//USE_OMP
#endif//OUTPUT_RPAVG

#ifndef USE_ISPC
  DOUBLE sqr_rpmax=rupp_sqr[nrpbin-1];
  DOUBLE sqr_rpmin=rupp_sqr[0];

#ifdef USE_AVX
  AVX_FLOATS m_rupp_sqr[nrpbin];
  for(int i=0;i<nrpbin;i++) {
    m_rupp_sqr[i] = AVX_SET_FLOAT(rupp_sqr[i]);
  }

#ifdef OUTPUT_RPAVG
  AVX_FLOATS m_kbin[nrpbin];
  for(int i=0;i<nrpbin;i++) {
    m_kbin[i] = AVX_SET_FLOAT((DOUBLE) i);
	}
#endif//RPAVG  

#endif//AVX
#endif//USE_ISPC

  int64_t totncells = (int64_t) nmesh_x * (int64_t) nmesh_y * (int64_t) nmesh_z;  
  /*---Loop-over-Data1-particles--------------------*/
#ifdef USE_OMP
#pragma omp parallel 
  {
		int tid = omp_get_thread_num();
/* 		unsigned int *npairs = all_npairs[tid]; */
		unsigned int npairs[nrpbin];
		for(int i=0;i<nrpbin;i++) npairs[i] = 0;
#ifdef OUTPUT_RPAVG		
		DOUBLE rpavg[nrpbin];
		for(int i=0; i < nrpbin;i++) {
			rpavg[i] = 0.0;
		}
#endif		


#pragma omp for  schedule(dynamic) 
#endif
		for(int icell=0;icell<totncells;icell++) {
		  cellarray *first = &(lattice1[icell]);
		  int iz = icell % nmesh_z ;
		  int ix = icell / (nmesh_z * nmesh_y) ;
		  int iy = (icell - iz - ix*nmesh_z*nmesh_y)/nmesh_z ;
		  assert( ((iz + nmesh_z*iy + nmesh_z*nmesh_y*ix) == icell) && "Index reconstruction is wrong");
		  for(int iix=-bin_refine_factor;iix<=bin_refine_factor;iix++){
				int iiix; 
				iiix = iix+ix;
				if(iiix < 0 || iiix >= nmesh_x) {
				  continue;
				}
				
				for(int iiy=-bin_refine_factor;iiy<=bin_refine_factor;iiy++){
				  int iiiy;
				  iiiy = iiy+iy;
				  if(iiiy < 0 || iiiy >= nmesh_y) {
						continue;
				  } 
				  
				  for(int iiz=-bin_refine_factor;iiz<=bin_refine_factor;iiz++){
						int iiiz;
						iiiz = iiz+iz;
						if(iiiz < 0 || iiiz >= nmesh_z) {
							continue;
						}
						assert(iiix >= 0 && iiix < nmesh_x && iiiy >= 0 && iiiy < nmesh_y && iiiz >= 0 && iiiz < nmesh_z && "Checking that the second pointer is in range");
						int64_t index2 = iiix*nmesh_y*nmesh_z + iiiy*nmesh_z + iiiz;
						cellarray * second = &(lattice2[index2]);
						const DOUBLE *x1 = first->x;
						const DOUBLE *y1 = first->y;
						const DOUBLE *z1 = first->z;
					
						const DOUBLE *x2 = second->x;
						const DOUBLE *y2 = second->y;
						const DOUBLE *z2 = second->z;
					
#ifndef USE_ISPC
					
						for(int i=0;i<first->nelements;i++) {
							DOUBLE x1pos=x1[i];
							DOUBLE y1pos=y1[i];
							DOUBLE z1pos=z1[i];
					  
#ifndef USE_AVX
							for(int j=0;j<second->nelements;j+=BLOCK_SIZE) {
								int block_size=second->nelements - j;
								if(block_size > BLOCK_SIZE) block_size=BLOCK_SIZE;
						
								for(int jj=0;jj<block_size;jj++) {
									DOUBLE dx = x1pos - x2[j+jj];
									DOUBLE dy = y1pos - y2[j+jj];
									DOUBLE dz = z1pos - z2[j+jj];
									DOUBLE r2 = (dx*dx + dy*dy + dz*dz);
									if(r2 >= sqr_rpmax || r2 < sqr_rpmin) {
										continue;
									}
#ifdef OUTPUT_RPAVG
									DOUBLE r = SQRT(r2);
#endif					
									for(int kbin=nrpbin-1;kbin>=1;kbin--){
										if(r2 >= rupp_sqr[kbin-1]) {
											npairs[kbin]++;
#ifdef OUTPUT_RPAVG						
											rpavg[kbin] += r;
#endif						
											break;
										}
									}//searching for kbin loop
								}
							}//end of j loop
					  
#else //beginning of AVX section
		
#ifdef OUTPUT_RPAVG
							union int8 {
								AVX_INTS m_ibin;
								int ibin[NVEC];
							};
							union int8 union_rpbin;
					  
							union float8{
								AVX_FLOATS m_Dperp;
								DOUBLE Dperp[NVEC];
							};
							union float8 union_mDperp;
#endif				
					  
							AVX_FLOATS m_x1pos = AVX_SET_FLOAT(x1pos);
							AVX_FLOATS m_y1pos = AVX_SET_FLOAT(y1pos);
							AVX_FLOATS m_z1pos = AVX_SET_FLOAT(z1pos);
					  
							int j;
							for(j=0;j<=(second->nelements-NVEC);j+=NVEC) {
								const AVX_FLOATS x2pos = AVX_LOAD_FLOATS_UNALIGNED(&x2[j]);
								const AVX_FLOATS y2pos = AVX_LOAD_FLOATS_UNALIGNED(&y2[j]);
								const AVX_FLOATS z2pos = AVX_LOAD_FLOATS_UNALIGNED(&z2[j]);
						
								const AVX_FLOATS m_xdiff = AVX_SUBTRACT_FLOATS(m_x1pos,x2pos);
								const AVX_FLOATS m_ydiff = AVX_SUBTRACT_FLOATS(m_y1pos,y2pos);
								const AVX_FLOATS m_zdiff = AVX_SUBTRACT_FLOATS(m_z1pos,z2pos);
								const AVX_FLOATS m_sqr_rpmax = AVX_SET_FLOAT(sqr_rpmax);
								const AVX_FLOATS m_sqr_rpmin = AVX_SET_FLOAT(sqr_rpmin);
								const AVX_FLOATS m_xdiff_sqr = AVX_SQUARE_FLOAT(m_xdiff);
								const AVX_FLOATS m_ydiff_sqr = AVX_SQUARE_FLOAT(m_ydiff);
								const AVX_FLOATS m_zdiff_sqr = AVX_SQUARE_FLOAT(m_zdiff);
								const AVX_FLOATS m_xydiff_sqr_sum = AVX_ADD_FLOATS(m_xdiff_sqr,m_ydiff_sqr);
						
								AVX_FLOATS r2 = AVX_ADD_FLOATS(m_zdiff_sqr,m_xydiff_sqr_sum);
								AVX_FLOATS m_mask_left;
						
								{
									m_mask_left = AVX_COMPARE_FLOATS(r2,m_sqr_rpmax,_CMP_LT_OS);
									if(AVX_TEST_COMPARISON(m_mask_left) == 0) {
										continue;
									}
						  
									AVX_FLOATS m_mask = AVX_BITWISE_AND(m_mask_left, AVX_COMPARE_FLOATS(r2, m_sqr_rpmin, _CMP_GE_OS));
									if(AVX_TEST_COMPARISON(m_mask) == 0) {
										continue;
									}
									r2 = AVX_BLEND_FLOATS_WITH_MASK(m_sqr_rpmax, r2, m_mask);
									m_mask_left = AVX_COMPARE_FLOATS(r2,m_sqr_rpmax,_CMP_LT_OS);
								}

								{ 
						  
#ifdef OUTPUT_RPAVG					  
									union_mDperp.m_Dperp = AVX_SQRT_FLOAT(r2);
									AVX_FLOATS m_rpbin = AVX_SET_FLOAT((DOUBLE) 0.0);
#endif 					  
						  
									/* AVX_FLOATS m_all_ones  = AVX_CAST_INT_TO_FLOAT(AVX_SET_INT(-1));//-1 is 0xFFFF... and the cast just reinterprets (i.e., the cast is a no-op) */
									for(int kbin=nrpbin-1;kbin>=1;kbin--) {
										AVX_FLOATS m1 = AVX_COMPARE_FLOATS(r2,m_rupp_sqr[kbin-1],_CMP_GE_OS);
										AVX_FLOATS m_bin_mask = AVX_BITWISE_AND(m1,m_mask_left);
										m_mask_left = AVX_COMPARE_FLOATS(r2,m_rupp_sqr[kbin-1],_CMP_LT_OS);
										/* m_mask_left = AVX_XOR_FLOATS(m1, m_all_ones);//XOR with 0xFFFF... gives the bins that are smaller than m_rupp_sqr[kbin] (and is faster than cmp_p(s/d) in theory) */
										int test2  = AVX_TEST_COMPARISON(m_bin_mask);
										npairs[kbin] += AVX_BIT_COUNT_INT(test2);
#ifdef OUTPUT_RPAVG
										m_rpbin = AVX_BLEND_FLOATS_WITH_MASK(m_rpbin,m_kbin[kbin], m_bin_mask);
#endif					  
										int test3 = AVX_TEST_COMPARISON(m_mask_left);
										if(test3 == 0)
											break;
									}
#ifdef OUTPUT_RPAVG
									union_rpbin.m_ibin = AVX_TRUNCATE_FLOAT_TO_INT(m_rpbin);
#endif					
								}
						
								//All these ops can be avoided (and anything leading to these) if the CPU
								//supports AVX 512 mask_add operation
#ifdef OUTPUT_RPAVG

//protect the unroll pragma in case compiler is not icc.								
#if  __INTEL_COMPILER
#pragma unroll(NVEC)
#endif
								for(int jj=0;jj<NVEC;jj++) {
									int kbin = union_rpbin.ibin[jj];
									DOUBLE r = union_mDperp.Dperp[jj];
									rpavg[kbin] += r;
								}
#endif//OUTPUT_RPAVG				  
							}
		  
							//Now take care of the remainder
							for(;j<second->nelements;j++) {
								DOUBLE dx = x1pos - x2[j];
								DOUBLE dy = y1pos - y2[j];
								DOUBLE dz = z1pos - z2[j];
						
								DOUBLE r2 = (dx*dx + dy*dy + dz*dz);
								if(r2 >= sqr_rpmax || r2 < sqr_rpmin) {
									continue;
								}
#ifdef OUTPUT_RPAVG
								DOUBLE r = SQRT(r2);
#endif				  
								for(int kbin=nrpbin-1;kbin>=1;kbin--){
									if(r2 >= rupp_sqr[kbin-1]) {
										npairs[kbin]++;
#ifdef OUTPUT_RPAVG
										rpavg[kbin] += r;
#endif					  
										break;
									}
								}//searching for kbin loop
							}//end of remainder j loop
					  
#endif//end of AVX section
					  
						}//end of i loop
#else //USE_ISPC section
						compute_distances_two_cells(first->nelements, x1, y1, z1,
																				second->nelements, x2, y2, z2,
																				nrpbin,rupp_sqr,npairs);
					
					
#endif //USE_ISPC section

				  }//iiz loop over bin_refine_factor
				}//iiy loop over bin_refine_factor
			}//iix loop over bin_refine_factor
			  
		}//icell loop over totncells
#ifdef USE_OMP
		for(int j=0;j<nrpbin;j++) {
		  all_npairs[tid][j] = npairs[j];
		}
#ifdef OUTPUT_RPAVG
		for(int j=0;j<nrpbin;j++) {
			all_rpavg[tid][j] = rpavg[j];
		}
#endif

	}//close the omp parallel region
#endif
  

#ifdef USE_OMP
  unsigned int npairs[nrpbin];
  for(int i=0;i<nrpbin;i++) npairs[i] = 0;
	
  for(int i=0;i<numthreads;i++) {
		for(int j=0;j<nrpbin;j++) {
			npairs[j] += all_npairs[i][j];
		}
  }
#ifdef OUTPUT_RPAVG
	DOUBLE rpavg[nrpbin];
	for(int i=0;i<nrpbin;i++) rpavg[i] = 0.0;

  for(int i=0;i<numthreads;i++) {
		for(int j=0;j<nrpbin;j++) {
			rpavg[j] += all_rpavg[i][j];
		}
	}
#endif

#endif


#ifdef OUTPUT_RPAVG  
  for(int i=0;i<nrpbin;i++) {
    if(npairs[i] > 0) {
      rpavg[i] /= (DOUBLE) npairs[i] ;
    }
  }
#endif
  
  double rlow=rupp[0];
  for(int i=1;i<nrpbin;i++) {
#ifdef OUTPUT_RPAVG	  
  	(*paircounts)[i] = npairs[i];
    fprintf(stdout,"%10u %20.8lf %20.8lf %20.8lf \n",npairs[i],rpavg[i],rlow,rupp[i]);
#else
    (*paircounts)[i] = npairs[i];
	fprintf(stdout,"%10u %20.8lf %20.8lf %20.8lf \n",npairs[i],0.0,    rlow,rupp[i]);
#endif	
    rlow=rupp[i];
  }



  for(int64_t i=0;i<totncells;i++) {
		free(lattice1[i].x);
		free(lattice1[i].y);
		free(lattice1[i].z);
		if(autocorr==0) {
			free(lattice2[i].x);
			free(lattice2[i].y);
			free(lattice2[i].z);
		}
  }
  
  free(lattice1);
  if(autocorr==0) {
	free(lattice2);
  }

#ifdef USE_OMP
  matrix_free((void **) all_npairs, numthreads);
#ifdef OUTPUT_RPAVG
	matrix_free((void **) all_rpavg, numthreads);
#endif
	
#endif



}