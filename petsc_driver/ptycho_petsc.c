
#include <stdlib.h>
#include <stdio.h>

#include <unistd.h>
#include <fcntl.h>

#include <mpi.h>

#include <petscsys.h>
#include <petscmat.h>
#include <petscksp.h>
#include "petscksp.h"

#include <stdlib.h>


int   M,N;
Mat   A;
KSP   ksp;          /* linear solver context */
PC    precond;
Vec   rhs;
Vec   sol;

void ptycho_setup_petsc(int argc,char **argv) {

	long size_in[2];

	printf("here is init_petsc\n");


	int fd = open("data/a_size.bin", O_RDONLY);
	if(fd < 0)  {
		printf("file not found:  a_size.bin\n");
		MPI_Abort(MPI_COMM_WORLD, -1);
	}
	size_t bytes_read = read(fd, size_in, 16);

	M = size_in[0];
	N = size_in[1];

	printf ("matrix size %d %d \n",M,N);
	PetscInitialize(&argc, &argv, NULL, NULL);

// Setup Matrix

	MatCreate(PETSC_COMM_WORLD,&A);
	MatSetSizes(A,PETSC_DECIDE,PETSC_DECIDE,M, N);
	MatSetFromOptions(A);
	MatSetUp(A);

// Setup rhs and solution vector

   VecCreateMPI (PETSC_COMM_WORLD, PETSC_DECIDE, M, &rhs);
   VecCreateMPI (PETSC_COMM_WORLD, PETSC_DECIDE, N, &sol);

// Setup ksp

	KSPCreate(PETSC_COMM_WORLD,&ksp);

// Setup Preconditioner

	PCCreate (PETSC_COMM_WORLD, &precond);

	return;
};

void ptycho_read_and_fill_Matrix () {

	int    i,irow;
	int    nval;
	size_t max_size = M*sizeof(int);
	int    *row_pointer;
	int    *indices;
	PetscScalar *values;

	row_pointer = malloc(max_size);
	indices     = malloc(max_size);
	values      = malloc(max_size*4);

	int fd = open("data/a_indpointer.bin", O_RDONLY);
	if(fd < 0)  {
		printf("file not found:  a_indpointer.bin\n");
		MPI_Abort(MPI_COMM_WORLD, -1);
	}
	size_t bytes_read = read(fd, row_pointer, max_size);

	int words_read = bytes_read/sizeof(int);
	printf ("ind_pointer bytes read %ld %d %ld %ld\n",bytes_read,words_read,max_size,sizeof(PetscScalar));

	int fd_ind = open("data/a_ind.bin", O_RDONLY);
	if(fd_ind < 0)  {
		printf("file not found:  a_ind.bin\n");
		MPI_Abort(MPI_COMM_WORLD, -1);
	}

	int fd_dat = open("data/a_data.bin", O_RDONLY);
	if(fd_dat < 0)  {
		printf("file not found:  a_data.bin\n");
		MPI_Abort(MPI_COMM_WORLD, -1);
	}

   for (irow=0; irow<words_read; irow++ ) {
   	if(irow <words_read-1 ) {
   		nval = row_pointer[irow+1] - row_pointer[irow];
   	} else {
   		nval = N+1 - row_pointer[irow];
   	}
   	size_t ind_nval = read (fd_ind, indices, nval*sizeof(int));
   	size_t val_nval = read (fd_dat, values, nval*sizeof(double complex));
   	MatSetValues (A, 1, &irow, nval, indices, values, INSERT_VALUES);

   	if(irow < 10) printf ("rows %d %d %d %ld %d\n",irow,row_pointer[irow],nval,ind_nval/sizeof(int),row_pointer[irow]);
   	if(irow < 10) printf ("row indices %d %ld %ld %f + i%f\n",irow,ind_nval,val_nval,creal(values[irow]),cimag(values[irow]));
   }

   MatAssemblyBegin (A, MAT_FINAL_ASSEMBLY);
   MatAssemblyEnd   (A, MAT_FINAL_ASSEMBLY);

	return;
}

void ptycho_read_and_set_RHS () {

	int    i;
	size_t max_size = M*sizeof(double);
	double *buf;
	int    *indices;
	PetscScalar *rhs_val;

	buf     = malloc(max_size);
	rhs_val= malloc(M*sizeof(PetscScalar));

	int fd = open("data/b_vector.bin", O_RDONLY);
	if(fd < 0)  {
		printf("file not found:  b_vector.bin\n");
		MPI_Abort(MPI_COMM_WORLD, -1);
	}
	size_t bytes_read = read(fd, buf, max_size*sizeof(double));

	int words_read = bytes_read/sizeof(double);
	printf ("RHS bytes read %ld %d %ld\n",bytes_read,words_read,max_size*sizeof(double));
	printf ("RHS values %f %f %f\n",buf[0],buf[1],buf[2]);

	indices = malloc(words_read*sizeof(int));

	for (i=0; i<words_read; i++) {
		indices[i] = i;
		rhs_val[i] = (PetscScalar) buf[i];          // convert double to double complex
	}
   VecSetValues (rhs, words_read, indices, rhs_val, INSERT_VALUES);

   VecAssemblyBegin (rhs);
   VecAssemblyEnd   (rhs);

	return;

}

void ptycho_petsc_solve () {

   enum solver_type {
   	GMRES,
		BICG,
		BCGS,
		FGMRES,
		DGMRES,
		GCR,
		RICHARDSON,
		CHEBYSHEV
   };

   enum preconditioner_type {
   	NOPC,
		JACOBI,
		SOR,
		EISENSTAT,
		ICC,
		ILU
   };

// select solver and preconditioner

	int solver_selected = BICG;
	int pc_selected     = NOPC;

	int gmres_max_restart_iter = 20;

   KSPSetOperators(ksp, A, A);

   KSPSetInitialGuessNonzero (ksp, PETSC_FALSE);


   switch(solver_selected) {
   	case GMRES:
   		KSPSetType (ksp, KSPGMRES);
   		if(gmres_max_restart_iter > 20) KSPGMRESSetRestart (ksp, gmres_max_restart_iter);
   		break;
   	case BICG:
   		KSPSetType (ksp, KSPBICG);
   		break;
   	case BCGS:
   		KSPSetType (ksp, KSPBCGS);
   		break;
   	case FGMRES:
   		KSPSetType (ksp, KSPFGMRES);
   		break;
   	case DGMRES:
   		KSPSetType (ksp, KSPDGMRES);
   		break;
   	case GCR:
   		KSPSetType (ksp, KSPGCR);
   		break;
   	case RICHARDSON:
   		KSPSetType (ksp, KSPRICHARDSON);
   		break;
   	case CHEBYSHEV:
   		KSPSetType (ksp, KSPCHEBYSHEV);
   		break;
   	default:
   		printf("illegal solver value %d\n",solver_selected);
   };

   KSPType solver_type_as_string;
   KSPGetType (ksp, &solver_type_as_string);
   printf("solver type %s\n",solver_type_as_string);

   KSPGetPC (ksp, &precond);

   switch(pc_selected) {
   	case NOPC:
   		PCSetType (precond, PCNONE);
   		break;
   	case JACOBI:
   		PCSetType (precond, PCJACOBI);
   		break;
   	case SOR:
   		PCSetType (precond, PCSOR);
   		break;
   	case EISENSTAT:
   		PCSetType (precond, PCEISENSTAT);
   		break;
   	case ICC:
   		PCSetType (precond, PCICC);
   		break;
   	case ILU:
   		PCSetType (precond, PCILU);
   		break;
   	default:
   		printf("illegal preconditioner value %d\n",solver_selected);
   };

   KSPType pc_type_as_string;
   PCGetType (precond, &pc_type_as_string);
   printf("preconditioner type %s\n",pc_type_as_string);

   PetscReal rtol;
   PetscReal abstol;
   PetscReal dtol;
   PetscInt  maxits;

   KSPGetTolerances(ksp, &rtol, &abstol, &dtol, &maxits);
   printf ("Default tolerances %f %f %f %d\n", rtol, abstol, dtol, maxits);

// Here: modify Tolerances

   KSPSetTolerances (ksp, rtol, abstol, dtol, maxits);

   KSPSolve (ksp, rhs, sol);

   PetscInt niter = 0;
   KSPGetIterationNumber(ksp, &niter);
   printf ("Number of Iterations %d\n",niter);

	return;
};

void ptycho_petsc_get_solution () {
	int   i;

	PetscScalar *x;

   KSPGetSolution (ksp, &sol);

   VecGetArray(sol, &x);

   for (i=0; i<10; i++) {
   	printf("solution value %d %f + i%f \n",i,creal(x[i]),cimag(x[i]));
   }

}

