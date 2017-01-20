/**************************************************************************************************
*                                                                                                 *
* This file is part of HPMPC.                                                                     *
*                                                                                                 *
* HPMPC -- Library for High-Performance implementation of solvers for MPC.                        *
* Copyright (C) 2014-2015 by Technical University of Denmark. All rights reserved.                *
*                                                                                                 *
* HPMPC is free software; you can redistribute it and/or                                          *
* modify it under the terms of the GNU Lesser General Public                                      *
* License as published by the Free Software Foundation; either                                    *
* version 2.1 of the License, or (at your option) any later version.                              *
*                                                                                                 *
* HPMPC is distributed in the hope that it will be useful,                                        *
* but WITHOUT ANY WARRANTY; without even the implied warranty of                                  *
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.                                            *
* See the GNU Lesser General Public License for more details.                                     *
*                                                                                                 *
* You should have received a copy of the GNU Lesser General Public                                *
* License along with HPMPC; if not, write to the Free Software                                    *
* Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA                  *
*                                                                                                 *
* Author: Gianluca Frison, giaf (at) dtu.dk                                                       *
*                                                                                                 *
**************************************************************************************************/

#include <stdlib.h>
#include <math.h>

#ifdef BLASFEO

#include <blasfeo_target.h>
#include <blasfeo_common.h>
#include <blasfeo_d_aux.h>
#include <blasfeo_d_blas.h>

//#else
#include "../include/blas_d.h"
//#endif

#include "../include/tree.h"

#include "../include/aux_d.h"
#include "../include/aux_s.h"
#include "../include/lqcp_solvers.h"
#include "../include/block_size.h"
#include "../include/mpc_aux.h"
#include "../include/d_blas_aux.h"



#define CORRECTOR_LOW 1



// work space size 
int d_tree_ip2_res_mpc_hard_work_space_size_bytes_libstr(int Nn, struct node *tree, int *nx, int *nu, int *nb, int *ng)
	{

	int ii;

	int size = 0;

	for(ii=0; ii<Nn; ii++)
		{
		size += d_size_strmat(nu[ii]+nx[ii]+1, nu[ii]+nx[ii]); // L
		size += d_size_strmat(nx[ii], nx[ii]); // Lxt
		size += 5*d_size_strvec(nx[ii]); // b, dpi, Pb, res_b, pi_bkp
		size += 4*d_size_strvec(nu[ii]+nx[ii]); // dux, rq, res_rq, ux_bkp
		size += 8*d_size_strvec(2*nb[ii]+2*ng[ii]); // dlam, dt, tinv, lamt, res_d, res_m, t_bkp, lam_bkp
		size += 2*d_size_strvec(nb[ii]+ng[ii]); // Qx, qx
		}

	// residuals work space size
	size += d_res_res_mpc_hard_work_space_size_bytes_libstr(Nn-1, nx, nu, nb, ng); // TODO

	// riccati work space size
	size += d_tree_back_ric_rec_work_space_size_bytes_libstr(Nn, tree, nx, nu, nb, ng);

	return size;
	}



/* primal-dual interior-point method computing residuals at each iteration, hard constraints, time variant matrices, time variant size (mpc version) */
int d_tree_ip2_res_mpc_hard_libstr(int *kk, int k_max, double mu0, double mu_tol, double alpha_min, int warm_start, double *stat, int Nn, struct node *tree, int *nx, int *nu, int *nb, int **idxb, int *ng, struct d_strmat *hsBAbt, struct d_strmat *hsRSQrq, struct d_strmat *hsDCt, struct d_strvec *hsd, struct d_strvec *hsux, int compute_mult, struct d_strvec *hspi, struct d_strvec *hslam, struct d_strvec *hst, void *work)
	{

	// adjust number of nodes
	int N = Nn-1;

	// indeces
	int jj, ll, ii, it_ref;
	int nkids, idxkid;



#if 0
printf("\nBAbt\n");
for(ii=0; ii<Nn; ii++)
	{
	nkids = tree[ii].nkids;
	for(jj=0; jj<nkids; jj++)
		{
		idxkid = tree[ii].kids[jj];
		d_print_strmat(nu[ii]+nx[ii]+1, nx[idxkid], &hsBAbt[idxkid], 0, 0);
		}
	}
printf("\nRSQrq\n");
for(ii=0; ii<Nn; ii++)
	d_print_strmat(nu[ii]+nx[ii]+1, nu[ii]+nx[ii], &hsRSQrq[ii], 0, 0);
//printf("\nd\n");
//for(ii=0; ii<=N; ii++)
//	d_print_mat(1, 2*pnb[ii]+2*png[ii], d[ii], 1);
exit(2);
#endif



	struct d_strmat *hsmatdummy;
	struct d_strvec *hsvecdummy;

	struct d_strvec hsb[N+1];
	struct d_strvec hsrq[N+1];
	struct d_strvec hsQx[N+1];
	struct d_strvec hsqx[N+1];
	struct d_strvec hsdux[N+1];
	struct d_strvec hsdpi[N+1];
	struct d_strvec hsdt[N+1];
	struct d_strvec hsdlam[N+1];
	struct d_strvec hstinv[N+1];
	struct d_strvec hslamt[N+1];
	struct d_strvec hsPb[N+1];
	struct d_strmat hsL[N+1];
	struct d_strmat hsLxt[N+1];
	struct d_strvec hsres_rq[N+1];
	struct d_strvec hsres_b[N+1];
	struct d_strvec hsres_d[N+1];
	struct d_strvec hsres_m[N+1];
	struct d_strmat hsric_work_mat[2];
	struct d_strvec hsric_work_vec[1];
	struct d_strvec hsres_work[2];

	void *d_tree_back_ric_rec_work_space;
	void *d_res_res_mpc_hard_work_space;

	char *c_ptr = work;

	// L
	for(ii=0; ii<=N; ii++)
		{
		d_create_strmat(nu[ii]+nx[ii]+1, nu[ii]+nx[ii], &hsL[ii], (void *) c_ptr);
		c_ptr += hsL[ii].memory_size;
		}

	// Lxt
	for(ii=0; ii<=N; ii++)
		{
		d_create_strmat(nx[ii], nx[ii], &hsLxt[ii], (void *) c_ptr);
		c_ptr += hsLxt[ii].memory_size;
		}

	// riccati work space
	d_tree_back_ric_rec_work_space = (void *) c_ptr;
	c_ptr += d_tree_back_ric_rec_work_space_size_bytes_libstr(Nn, tree, nx, nu, nb, ng);

	// b as vector
	for(ii=0; ii<Nn; ii++)
		{
		nkids = tree[ii].nkids;
		for(jj=0; jj<nkids; jj++)
			{
			idxkid = tree[ii].kids[jj];
//			b[idxkid] = ptr;
//			ptr += pnx[idxkid];
			d_create_strvec(nx[idxkid], &hsb[idxkid], (void *) c_ptr);
			c_ptr += hsb[idxkid].memory_size;
			}
		}

	// inputs and states step
	for(ii=0; ii<=N; ii++)
		{
		d_create_strvec(nu[ii]+nx[ii], &hsdux[ii], (void *) c_ptr);
		c_ptr += hsdux[ii].memory_size;
		}

	// equality constr multipliers
	for(ii=0; ii<Nn; ii++)
		{
		nkids = tree[ii].nkids;
		for(jj=0; jj<nkids; jj++)
			{
			idxkid = tree[ii].kids[jj];
			d_create_strvec(nx[idxkid], &hsdpi[idxkid], (void *) c_ptr);
			c_ptr += hsdpi[idxkid].memory_size;
			}
		}
	
	// backup of P*b
	for(ii=0; ii<Nn; ii++)
		{
		nkids = tree[ii].nkids;
		for(jj=0; jj<nkids; jj++)
			{
			idxkid = tree[ii].kids[jj];
			d_create_strvec(nx[idxkid], &hsPb[idxkid], (void *) c_ptr);
			c_ptr += hsPb[idxkid].memory_size;
			}
		}
	
	// linear part of cost function
	for(ii=0; ii<=N; ii++)
		{
		d_create_strvec(nu[ii]+nx[ii], &hsrq[ii], (void *) c_ptr);
		c_ptr += hsrq[ii].memory_size;
		}

	// slack variables, Lagrangian multipliers for inequality constraints and work space
	for(ii=0; ii<=N; ii++)
		{
		d_create_strvec(2*nb[ii]+2*ng[ii], &hsdlam[ii], (void *) c_ptr);
		c_ptr += hsdlam[ii].memory_size;
		d_create_strvec(2*nb[ii]+2*ng[ii], &hsdt[ii], (void *) c_ptr);
		c_ptr += hsdt[ii].memory_size;
		}

	for(ii=0; ii<=N; ii++)
		{
		d_create_strvec(2*nb[ii]+2*ng[ii], &hstinv[ii], (void *) c_ptr);
		c_ptr += hstinv[ii].memory_size;
		}

	for(ii=0; ii<=N; ii++)
		{
		d_create_strvec(2*nb[ii]+2*ng[ii], &hslamt[ii], (void *) c_ptr);
		c_ptr += hslamt[ii].memory_size;
		}

	for(ii=0; ii<=N; ii++)
		{
		d_create_strvec(nb[ii]+ng[ii], &hsQx[ii], (void *) c_ptr);
		c_ptr += hsQx[ii].memory_size;
		d_create_strvec(nb[ii]+ng[ii], &hsqx[ii], (void *) c_ptr);
		c_ptr += hsqx[ii].memory_size;
		}


	// residuals work space
	d_res_res_mpc_hard_work_space = (void *) c_ptr;
	c_ptr += d_res_res_mpc_hard_work_space_size_bytes_libstr(N, nx, nu, nb, ng); // TODO

	for(ii=0; ii<=N; ii++)
		{
		d_create_strvec(nu[ii]+nx[ii], &hsres_rq[ii], (void *) c_ptr);
		c_ptr += hsres_rq[ii].memory_size;
		}

	for(ii=0; ii<Nn; ii++)
		{
		nkids = tree[ii].nkids;
		for(jj=0; jj<nkids; jj++)
			{
			idxkid = tree[ii].kids[jj];
			d_create_strvec(nx[idxkid], &hsres_b[idxkid], (void *) c_ptr);
			c_ptr += hsres_b[idxkid].memory_size;
			}
		}

	for(ii=0; ii<=N; ii++)
		{
		d_create_strvec(2*nb[ii]+2*ng[ii], &hsres_d[ii], (void *) c_ptr);
		c_ptr += hsres_d[ii].memory_size;
		}

	for(ii=0; ii<=N; ii++)
		{
		d_create_strvec(2*nb[ii]+2*ng[ii], &hsres_m[ii], (void *) c_ptr);
		c_ptr += hsres_m[ii].memory_size;
		}

	// extract arrays
	double *hpRSQrq[N+1];
	for(jj=0; jj<=N; jj++)
		hpRSQrq[jj] = hsRSQrq[jj].pA;

	// extract b
	for(ii=0; ii<Nn; ii++)
		{
		nkids = tree[ii].nkids;
		for(jj=0; jj<nkids; jj++)
			{
			idxkid = tree[ii].kids[jj];
			drowex_libstr(nx[idxkid], 1.0, &hsBAbt[idxkid], nu[ii]+nx[ii], 0, &hsb[idxkid], 0);
			drowex_libstr(nx[idxkid], 1.0, &hsBAbt[idxkid], nu[ii]+nx[ii], 0, &hsb[idxkid], 0);
			}
		}
	
	// extract q
	for(jj=0; jj<=N; jj++)
		{
		drowex_libstr(nu[jj]+nx[jj], 1.0, &hsRSQrq[jj], nu[jj]+nx[jj], 0, &hsrq[jj], 0);
		}



	double temp0, temp1;
	double alpha, mu, mu_aff;

	// check if there are inequality constraints
	double mu_scal = 0.0; 
	for(jj=0; jj<=N; jj++) mu_scal += 2*nb[jj] + 2*ng[jj];
	if(mu_scal!=0.0) // there are some constraints
		{
		mu_scal = 1.0 / mu_scal;
		}
	else // call the riccati solver and return
		{
		double **dummy;
#if 0
		d_back_ric_rec_sv_libstr(N, nx, nu, nb, idxb, ng, 0, hsBAbt, hsb, 0, hsRSQrq, hsrq, hsvecdummy, hsmatdummy, hsvecdummy, hsvecdummy, hsux, compute_mult, hspi, 1, hsPb, hsL, hsLxt, hsric_work_mat, hsric_work_vec);
#else
		d_tree_back_ric_rec_trf_libstr(Nn, tree, nx, nu, nb, idxb, ng, hsBAbt, hsRSQrq, hsmatdummy, hsvecdummy, hsL, hsLxt, d_tree_back_ric_rec_work_space);
		d_tree_back_ric_rec_trs_libstr(Nn, tree, nx, nu, nb, idxb, ng, hsBAbt, hsb, hsrq, hsmatdummy, hsvecdummy, hsux, compute_mult, hspi, 1, hsPb, hsL, hsLxt, d_tree_back_ric_rec_work_space);
#endif
		// no IPM iterations
		*kk = 0;
		// return success
		return 0;
		}

	//printf("\nmu_scal = %f\n", mu_scal);
	double sigma = 0.0;
	//for(ii=0; ii<=N; ii++)
	//	printf("\n%d %d\n", nb[ii], ng[ii]);
	//exit(1);



	// initialize ux & pi & t>0 & lam>0
	// TODO version for tree, using Nn, and idxkid for pi
	d_init_var_tree_mpc_hard_libstr(Nn, tree, nx, nu, nb, idxb, ng, hsux, hspi, hsDCt, hsd, hst, hslam, mu0, warm_start);

#if 0
printf("\nux\n");
for(ii=0; ii<=N; ii++)
	d_print_tran_strvec(nu[ii]+nx[ii], &hsux[ii], 0);
printf("\npi\n");
//for(ii=0; ii<N; ii++)
//	d_print_mat(1, nx[ii+1], pi[ii+1], 1);
printf("\nlam\n");
for(ii=0; ii<=N; ii++)
	d_print_tran_strvec(2*nb[ii]+2*ng[ii], &hslam[ii], 0);
printf("\nt\n");
for(ii=0; ii<=N; ii++)
	d_print_tran_strvec(2*nb[ii]+2*ng[ii], &hst[ii], 0);
exit(1);
#endif



	// compute the duality gap
	mu = mu0;

	// set to zero iteration count
	*kk = 0;	

	// larger than minimum accepted step size
	alpha = 1.0;





	//
	// loop without residuals compuation at early iterations
	//

	double mu_tol_low = mu_tol;

#if 0
	if(0)
#else
	while( *kk<k_max && mu>mu_tol_low && alpha>=alpha_min )
#endif
		{

//		printf("\nkk = %d (no res)\n", *kk);
						


		//update cost function matrices and vectors (box constraints)
		d_update_hessian_mpc_hard_libstr(N, nx, nu, nb, ng, hsd, 0.0, hst, hstinv, hslam, hslamt, hsdlam, hsQx, hsqx);

#if 0
//for(ii=0; ii<=N; ii++)
//	d_print_tran_strvec(2*nb[ii]+2*ng[ii], &hslam[ii], 0);
//for(ii=0; ii<=N; ii++)
//	d_print_tran_strvec(2*nb[ii]+2*ng[ii], &hst[ii], 0);
printf("\nQx\n");
for(ii=0; ii<=N; ii++)
	d_print_tran_strvec(nb[ii]+ng[ii], &hsQx[ii], 0);
printf("\nqx\n");
for(ii=0; ii<=N; ii++)
	d_print_tran_strvec(nb[ii]+ng[ii], &hsqx[ii], 0);
//if(*kk==1)
//exit(1);
#endif


		// compute the search direction: factorize and solve the KKT system
#if 0
		d_back_ric_rec_sv_libstr(N, nx, nu, nb, idxb, ng, 0, hsBAbt, hsb, 1, hsRSQrq, hsrq, hsDCt, hsQx, hsqx, hsdux, compute_mult, hsdpi, 1, hsPb, hsL, hsLxt, hsric_work_mat, hsric_work_vec);
#else
		d_tree_back_ric_rec_trf_libstr(Nn, tree, nx, nu, nb, idxb, ng, hsBAbt, hsRSQrq, hsDCt, hsQx, hsL, hsLxt, d_tree_back_ric_rec_work_space);
		d_tree_back_ric_rec_trs_libstr(Nn, tree, nx, nu, nb, idxb, ng, hsBAbt, hsb, hsrq, hsDCt, hsqx, hsdux, compute_mult, hsdpi, 1, hsPb, hsL, hsLxt, d_tree_back_ric_rec_work_space);
#endif


#if 0
for(ii=0; ii<=N; ii++)
	d_print_pmat(nu[ii]+nx[ii]+1, nu[ii]+nx[ii]+1, bs, pQ[ii], cnux[ii]);
exit(1);
#endif
#if 0
for(ii=0; ii<=N; ii++)
	d_print_pmat(pnz[ii], cnux[ii], hpL[ii], cnux[ii]);
if(*kk==2)
exit(1);
#endif
#if 0
printf("\ndux\n");
for(ii=0; ii<=N; ii++)
	d_print_tran_strvec(nu[ii]+nx[ii], &hsdux[ii], 0);
//printf("\ndpi\n");
//for(ii=1; ii<=N; ii++)
//	d_print_mat(1, nx[ii], dpi[ii], 1);
//if(*kk==2)
//exit(1);
#endif


#if CORRECTOR_LOW==1 // IPM1

		// compute t_aff & dlam_aff & dt_aff & alpha
		alpha = 1.0;
		d_compute_alpha_mpc_hard_libstr(N, nx, nu, nb, idxb, ng, &alpha, hst, hsdt, hslam, hsdlam, hslamt, hsdux, hsDCt, hsd);

		

		stat[5*(*kk)] = sigma;
		stat[5*(*kk)+1] = alpha;
			
		alpha *= 0.995;

#if 0
printf("\nalpha = %f\n", alpha);
exit(1);
#endif


		// compute the affine duality gap
		d_compute_mu_mpc_hard_libstr(N, nx, nu, nb, ng, &mu_aff, mu_scal, alpha, hslam, hsdlam, hst, hsdt);

		stat[5*(*kk)+2] = mu_aff;

#if 0
printf("\nmu = %f\n", mu_aff);
exit(1);
#endif



		// compute sigma
		sigma = mu_aff/mu;
		sigma = sigma*sigma*sigma;
//		if(sigma<sigma_min)
//			sigma = sigma_min;
//printf("\n%f %f %f %f\n", mu_aff, mu, sigma, mu_scal);
//exit(1);

#if 0
for(ii=0; ii<=N; ii++)
	d_print_mat(1, 2*pnb[ii], dt[ii], 1);
for(ii=0; ii<=N; ii++)
	d_print_mat(1, 2*pnb[ii], dlam[ii], 1);
for(ii=0; ii<=N; ii++)
	d_print_mat(1, 2*pnb[ii], t_inv[ii], 1);
for(ii=0; ii<=N; ii++)
	d_print_mat(1, nb[ii], pl[ii], 1);
//exit(1);
#endif


//		d_update_gradient_mpc_hard_tv(N, nx, nu, nb, ng, sigma*mu, dt, dlam, t_inv, pl, qx);
		d_update_gradient_mpc_hard_libstr(N, nx, nu, nb, ng, sigma*mu, hsdt, hsdlam, hstinv, hsqx);

#if 0
for(ii=0; ii<=N; ii++)
	d_print_mat(1, nb[ii]+ng[ii], qx[ii], 1);
//if(*kk==1)
exit(1);
#endif



//		// copy b into x
//		for(ii=0; ii<N; ii++)
//			for(jj=0; jj<nx[ii+1]; jj++) 
//				dux[ii+1][nu[ii+1]+jj] = pBAbt[ii][(nu[ii]+nx[ii])/bs*bs*cnx[ii+1]+(nu[ii]+nx[ii])%bs+bs*jj]; // copy b



		// solve the system
		d_tree_back_ric_rec_trs_libstr(Nn, tree, nx, nu, nb, idxb, ng, hsBAbt, hsb, hsrq, hsDCt, hsqx, hsdux, compute_mult, hsdpi, 0, hsPb, hsL, hsLxt, d_tree_back_ric_rec_work_space);

#if 0
printf("\ndux\n");
for(ii=0; ii<=N; ii++)
	d_print_mat(1, nu[ii]+nx[ii], dux[ii], 1);
//if(*kk==1)
exit(1);
#endif



#endif // end of IPM1


		// compute t & dlam & dt & alpha
		alpha = 1.0;
		d_compute_alpha_mpc_hard_libstr(N, nx, nu, nb, idxb, ng, &alpha, hst, hsdt, hslam, hsdlam, hslamt, hsdux, hsDCt, hsd);

#if 0
printf("\nalpha = %f\n", alpha);
printf("\ndt\n");
for(ii=0; ii<=N; ii++)
	d_print_mat(1, 2*nb[ii]+2*ng[ii], dt[ii], 1);
printf("\ndlam\n");
for(ii=0; ii<=N; ii++)
	d_print_mat(1, 2*nb[ii]+2*ng[ii], dlam[ii], 1);
exit(2);
#endif

		stat[5*(*kk)] = sigma;
		stat[5*(*kk)+3] = alpha;
			
		alpha *= 0.995;



		// compute step & update x, u, lam, t & compute the duality gap mu
		// TODO version for tree, using Nn, and idxkid for pi
		d_update_var_tree_mpc_hard_libstr(Nn, tree, nx, nu, nb, ng, &mu, mu_scal, alpha, hsux, hsdux, hst, hsdt, hslam, hsdlam, hspi, hsdpi);

		stat[5*(*kk)+4] = mu;
		

#if 0
printf("\nux\n");
for(ii=0; ii<=N; ii++)
	d_print_tran_strvec(nu[ii]+nx[ii], &hsux[ii], 0);
printf("\npi\n");
for(ii=1; ii<=N; ii++)
	d_print_tran_strvec(nx[ii], &hspi[ii], 0);
printf("\nlam\n");
for(ii=0; ii<=N; ii++)
	d_print_tran_strvec(2*nb[ii]+2*ng[ii], &hslam[ii], 0);
printf("\nt\n");
for(ii=0; ii<=N; ii++)
	d_print_tran_strvec(2*nb[ii]+2*ng[ii], &hst[ii], 0);
//if(*kk==1)
exit(1);
#endif


		// increment loop index
		(*kk)++;


		} // end of IP loop
	

	// restore Hessian
	for(jj=0; jj<=N; jj++)
		{
		drowin_libstr(nu[jj]+nx[jj], 1.0, &hsrq[jj], 0, &hsRSQrq[jj], nu[jj]+nx[jj], 0);
		}

#if 0
printf("\nux\n");
for(jj=0; jj<=N; jj++)
	d_print_mat(1, nu[jj]+nx[jj], ux[jj], 1);
printf("\npi\n");
for(jj=1; jj<=N; jj++)
	d_print_mat(1, nx[jj], pi[jj], 1);
printf("\nlam\n");
for(ii=0; ii<=N; ii++)
	d_print_mat(1, 2*nb[ii]+2*ng[ii], lam[ii], 1);
printf("\nt\n");
for(ii=0; ii<=N; ii++)
	d_print_mat(1, 2*nb[ii]+2*ng[ii], t[ii], 1);
exit(2);
#endif


	//
	// loop with residuals computation and iterative refinement for high-accuracy result
	//

	// compute residuals
//	d_res_res_mpc_hard_libstr(N, nx, nu, nb, idxb, ng, hsBAbt, hsb, hsRSQrq, hsrq, hsux, hsDCt, hsd, hspi, hslam, hst, hsres_rq, hsres_b, hsres_d, hsres_m, &mu, d_res_res_mpc_hard_work_space); // XXX crashed since dynamic is indexed differently // TODO
#if 0
	printf("\nres_rq\n");
	for(jj=0; jj<=N; jj++)
		d_print_mat_e(1, nu[jj]+nx[jj], res_rq[jj], 1);
	printf("\nres_b\n");
	for(jj=1; jj<=N; jj++)
		d_print_mat_e(1, nx[jj], res_b[jj], 1);
	printf("\nres_d\n");
	for(jj=0; jj<=N; jj++)
		d_print_mat_e(1, 2*nb[jj]+2*ng[jj], res_d[jj], 1);
	printf("\nres_m\n");
	for(jj=0; jj<=N; jj++)
		d_print_mat_e(1, 2*nb[jj]+2*ng[jj], res_m[jj], 1);
	printf("\nmu\n");
	d_print_mat_e(1, 1, &mu, 1);
	exit(2);
#endif


//exit(2);

	// successful exit
	if(mu<=mu_tol)
		return 0;
	
	// max number of iterations reached
	if(*kk>=k_max)
		return 1;
	
	// no improvement
	if(alpha<alpha_min)
		return 2;
	
	// impossible
	return -1;

	} // end of ipsolver



#endif
