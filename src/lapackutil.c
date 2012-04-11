/**********************************************************************
 *
 * lapackutil.c
 *
 * copyright (c) 2006-2012, Hao Wu and Karl Broman
 *
 * last modified Apr, 2012
 * first written Jan, 2006
 *
 *     This program is free software; you can redistribute it and/or
 *     modify it under the terms of the GNU General Public License,
 *     version 3, as published by the Free Software Foundation.
 *
 *     This program is distributed in the hope that it will be useful,
 *     but without any warranty; without even the implied warranty of
 *     merchantability or fitness for a particular purpose.  See the GNU
 *     General Public License, version 3, for more details.
 *
 *     A copy of the GNU General Public License, version 3, is available
 *     at http://www.r-project.org/Licenses/GPL-3
 *
 * C functions for the R/qtl package
 *
 * These are some wrapper functions for several LAPACK routines.
 *
 * Contains: mydgelss, mydgemm, mydpotrf, mydpotrs
 *           linreg_rss, setup_linreg_rss;
 *
 **********************************************************************/

#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <R.h>
#include <Rmath.h>
#include <R_ext/PrtUtil.h>
#include <R_ext/Applic.h>
#include <R_ext/Lapack.h>
#include "lapackutil.h"
#include "util.h"
#define TOL 1e-12


/* DGELSS function */
void mydgelss (int *n_ind, int *ncolx0, int *nphe, double *x0, double *x0_bk,
               double *pheno, double *tmppheno, double *s, double *tol,
               int *rank, double *work, int *lwork, int *info)
{
  int i, singular=0;

  /* use dgels first */
  F77_CALL(dgels)("N", n_ind, ncolx0, nphe, x0, n_ind, tmppheno, n_ind,
		  work, lwork, info);

  /* if there's problem like singular, use dgelss */
  /* note that x0 will contain the result for QR decomposition.
  If any diagonal element of R is zero, then input x0 is rank deficient */
  for(i=0; i<*ncolx0; i++)  {
    if(fabs(x0[*n_ind*i+i]) < TOL) {
      singular = 1;
      break;
    }
  }


  if(singular) { /* switch to dgelss if input x0 is not of full rank */
    /* note that tmppheno and x0 have been destroyed already,
    we need to make another copy of them */
    /*mexPrintf("Warning - Design matrix is signular \n"); */
    /* note that at this stage both x0 and tmppheno might be destroyed,
    we need to make a copy of them */

    memcpy(x0, x0_bk, *n_ind*(*ncolx0)*sizeof(double));
    memcpy(tmppheno, pheno, *n_ind*(*nphe)*sizeof(double));
    F77_CALL(dgelss) (n_ind, ncolx0, nphe, x0, n_ind, tmppheno, n_ind,
      s, tol, rank, work, lwork, info);
  }
}


/* DGEMM */
void mydgemm(int *nphe, int *n_ind, double *alpha, double *tmppheno,
             double *beta, double *rss_det)
{
  F77_CALL(dgemm)("T", "N", nphe, nphe, n_ind, alpha, tmppheno, n_ind,
             tmppheno, n_ind, beta, rss_det, nphe);
}

/* DPOTRF */
void mydpotrf(int *nphe, double *rss_det, int *info)
{
  F77_CALL(dpotrf)("U", nphe, rss_det, nphe, info);
}

/*DPOTRS */
void mydpotrs(char *uplo, int *n, int *nrhs, double *A,
              int *lda, double *B, int *ldb, int *info)
{
  F77_CALL(dpotrs)(uplo, n, nrhs, A, lda, B, ldb, info);
}


/* set up workspaces for linreg_rss */
void setup_linreg_rss(int nrow, int ncolx, int ncoly,
                      int *n_dwork, double **dwork, int **jpvt)
{
  int mn;

  *jpvt = (int *)R_alloc(ncolx, sizeof(int));

  mn = MIN(nrow, ncolx);

  *n_dwork = MAX(mn + MAX(mn, ncoly), MAX(mn + 3*ncolx + 1, 2*mn*ncoly));
  *dwork = (double *)R_alloc(*n_dwork, sizeof(double));
}

/* linear regression (just to calculate RSS) by LAPACK functions dgelsy and dgels */
void linreg_rss(int nrow, int ncolx, double *x, int ncoly, double *y,
                double *rss, int n_dwork, double *dwork, int *jpvt,
                double *xcopy, double *ycopy, double tol, 
                int skip_dgels, int verbose)
{
  int i, j, k, lda, ldb, info, rank, row_index, singular=0;
  double fitted;
  char notranspose='N';

  lda=nrow;
  ldb=nrow;

  /* fill rss and jpvt with 0's */
  for(i=0; i<ncoly; i++) rss[i] = 0.0;

  if(!skip_dgels) {
    /* first try dgels */
    F77_CALL(dgels)(&notranspose, &nrow, &ncolx, &ncoly, x, &lda, y, &ldb,
                    dwork, &n_dwork, &info);

    /* x contains QR decomposition; if diagonal element is zero, input x is rank deficient */
    rank = ncolx;
    for(i=0; i<ncolx; i++)  {
      if(fabs(x[i+ncolx*i]) < tol) {
        singular = 1;
        break;
      }
    }
    if(verbose && !singular) Rprintf(" -dgels worked\n");
  }

  if(skip_dgels || singular) {
    if(verbose) Rprintf(" -Running dgelsy\n");

    if(!skip_dgels) {
      /* restore x and y */
      memcpy(y, ycopy, nrow*ncoly*sizeof(double));
      memcpy(x, xcopy, nrow*ncolx*sizeof(double));
    }

    /* 0's in jpvt */
    for(i=0; i<ncolx; i++) jpvt[i] = 0;

    /* use dgelsy just to determine which x columns to use */
    F77_CALL(dgelsy)(&nrow, &ncolx, &ncoly, x, &lda, y, &ldb, jpvt, &tol,
                     &rank, dwork, &n_dwork, &info);

    if(verbose) Rprintf(" -rank = %d\n", rank);

    if(rank < ncolx) { /* x has < full rank */
      if(verbose) Rprintf("Calculating fitted values\n");

      /* calculate fitted values */
      for(j=0; j<ncoly; j++) {
        for(i=0; i<nrow; i++) {
          fitted = 0.0;
          for(k=0; k<ncolx; k++)
            fitted += (xcopy[i+k*nrow] * y[k+j*nrow]);
          rss[j] += (ycopy[i+j*nrow] - fitted)*(ycopy[i+j*nrow] - fitted);
        }
      }
      return;
    }
  }

  if(verbose) Rprintf("Calculating RSS\n");
  /* calculate RSS */
  row_index = 0;
  for(i=0; i<ncoly; i++) {
    for(j=rank; j<nrow; j++)
      rss[i] += y[row_index+j]*y[row_index+j];
    row_index += nrow;
  }
}

void R_linreg_rss(int *nrow, int *ncolx, double *x, int *ncoly, double *y,
                  double *rss, double *tol, int *skip_dgels, int *verbose)
{
  int n_dwork, *jpvt;
  double *dwork, *xcopy, *ycopy;

  xcopy = (double *)R_alloc(*nrow * *ncolx, sizeof(double));
  ycopy = (double *)R_alloc(*nrow * *ncoly, sizeof(double));

  memcpy(ycopy, y, *nrow * *ncoly * sizeof(double));
  memcpy(xcopy, x, *nrow * *ncolx * sizeof(double));

  setup_linreg_rss(*nrow, *ncolx, *ncoly, &n_dwork, &dwork, &jpvt);
  linreg_rss(*nrow, *ncolx, x, *ncoly, y, rss, n_dwork, dwork, jpvt,
             xcopy, ycopy, *tol, *skip_dgels, *verbose);
}

/* end of lapackutil.c */
