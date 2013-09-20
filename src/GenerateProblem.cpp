
//@HEADER
// ************************************************************************
// 
//               HPCG: Simple Conjugate Gradient Benchmark Code
// Questions? Contact Michael A. Heroux (maherou@sandia.gov) 
// 
// ************************************************************************
//@HEADER

/*!
 @file GenerateProblem.cpp

 HPCG routine
 */

#if defined(HPCG_DEBUG) || defined(HPCG_DETAILED_DEBUG)
#include <fstream>
using std::endl;
#include "hpcg.hpp"
#endif
#include <cassert>

#include "GenerateProblem.hpp"

#ifndef HPCG_NOMPI
#include <mpi.h>
#endif

#ifndef HPCG_NOOPENMP
#include <omp.h>
#endif

/*!
  Routine to read a sparse matrix, right hand side, initial guess, and exact
  solution (as computed by a direct solver).

  @param[in]  geom   data structure that stores the parallel run parameters and the factoring of total number of processes into three dimensional grid
  @param[in]  A      The known system matrix
  @param[out] b      The newly allocated and generated right hand side vector
  @param[out] x      The newly allocated solution vector with entries set to 0.0
  @param[out] xexact The newly allocated solution vector with entries set to the exact solution

  @see GenerateGeometry
*/

void GenerateProblem(const Geometry & geom, SparseMatrix & A, double **b, double **x, double **xexact) {

	int nx = geom.nx;
	int ny = geom.ny;
	int nz = geom.nz;
	int npx = geom.npx;
	int npy = geom.npy;
	int npz = geom.npz;
	int ipx = geom.ipx;
	int ipy = geom.ipy;
	int ipz = geom.ipz;
	int gnx = nx*npx;
	int gny = ny*npy;
	int gnz = nz*npz;

	local_int_t localNumberOfRows = nx*ny*nz; // This is the size of our subblock
        // If this assert fails, it most likely means that the local_int_t is set to int and should be set to long long
        assert(localNumberOfRows>0); // Throw an exception of the number of rows is less than zero (can happen if int overflow)
	int numberOfNonzerosPerRow = 27; // We are approximating a 27-point finite element/volume/difference 3D stencil

	global_int_t totalNumberOfRows = localNumberOfRows*geom.size; // Total number of grid points in mesh
        // If this assert fails, it most likely means that the global_int_t is set to int and should be set to long long
        assert(totalNumberOfRows>0); // Throw an exception of the number of rows is less than zero (can happen if int overflow)


	// Allocate arrays that are of length localNumberOfRows
	char * nonzerosInRow = new char[localNumberOfRows];
	global_int_t ** mtxIndG = new global_int_t*[localNumberOfRows];
	local_int_t  ** mtxIndL = new local_int_t*[localNumberOfRows];
	double ** matrixValues = new double*[localNumberOfRows];
	double ** matrixDiagonal = new double*[localNumberOfRows];

	*x = new double[localNumberOfRows];
	*b = new double[localNumberOfRows];
	*xexact = new double[localNumberOfRows];
	A.localToGlobalMap.resize(localNumberOfRows);

	// Use a parallel loop to do initial assignment:
	// distributes the physical placement of arrays of pointers across the memory system
#ifndef HPCG_NOOPENMP
#pragma omp parallel for
#endif
	for (local_int_t i=0; i< localNumberOfRows; ++i) {
		matrixValues[i] = 0;
		matrixDiagonal[i] = 0;
		mtxIndG[i] = 0;
		mtxIndL[i] = 0;
	}
	// Now allocate the arrays pointed to
	for (local_int_t i=0; i< localNumberOfRows; ++i) {
		mtxIndL[i] = new local_int_t[numberOfNonzerosPerRow];
		matrixValues[i] = new double[numberOfNonzerosPerRow];
		mtxIndG[i] = new global_int_t[numberOfNonzerosPerRow];
	}



	local_int_t localNumberOfNonzeros = 0;
	// TODO:  This triply nested loop could be flattened or use nested parallelism
#ifndef HPCG_NOOPENMP
#pragma omp parallel for
#endif
	for (local_int_t iz=0; iz<nz; iz++) {
		global_int_t giz = ipz*nz+iz;
		for (local_int_t iy=0; iy<ny; iy++) {
			global_int_t giy = ipy*ny+iy;
			for (local_int_t ix=0; ix<nx; ix++) {
				global_int_t gix = ipx*nx+ix;
				local_int_t currentLocalRow = iz*nx*ny+iy*nx+ix;
				global_int_t currentGlobalRow = giz*gnx*gny+giy*gnx+gix;
#ifndef HPCG_NOOPENMP
// C++ std::map is not threadsafe for writing
#pragma omp critical
#endif
				A.globalToLocalMap[currentGlobalRow] = currentLocalRow;

				A.localToGlobalMap[currentLocalRow] = currentGlobalRow;
#ifdef HPCG_DETAILED_DEBUG
				HPCG_fout << " rank, globalRow, localRow = " << geom.rank << " " << currentGlobalRow << " " << A.globalToLocalMap[currentGlobalRow] << endl;
#endif
				char numberOfNonzerosInRow = 0;
				double * currentValuePointer = matrixValues[currentLocalRow]; // Pointer to current value in current row
				global_int_t * currentIndexPointerG = mtxIndG[currentLocalRow]; // Pointer to current index in current row
				for (int sz=-1; sz<=1; sz++) {
					if (giz+sz>-1 && giz+sz<gnz) {
						for (int sy=-1; sy<=1; sy++) {
							if (giy+sy>-1 && giy+sy<gny) {
								for (int sx=-1; sx<=1; sx++) {
									if (gix+sx>-1 && gix+sx<gnx) {
										global_int_t curcol = currentGlobalRow+sz*gnx*gny+sy*gnx+sx;
										if (curcol==currentGlobalRow) {
											matrixDiagonal[currentLocalRow] = currentValuePointer;
											*currentValuePointer++ = 27.0;
										}
										else {
											*currentValuePointer++ = -1.0;
										}
										*currentIndexPointerG++ = curcol;
										numberOfNonzerosInRow++;
									} // end x bounds test
								} // end sx loop
							} // end y bounds test
						} // end sy loop
					} // end z bounds test
				} // end sz loop
				nonzerosInRow[currentLocalRow] = numberOfNonzerosInRow;
#ifndef HPCG_NOOPENMP
#pragma omp critical
#endif
				localNumberOfNonzeros += numberOfNonzerosInRow; // Protect this with an atomic
				(*x)[currentLocalRow] = 0.0;
				(*b)[currentLocalRow] = 27.0 - ((double) (numberOfNonzerosInRow-1));
				(*xexact)[currentLocalRow] = 1.0;
			} // end ix loop
		} // end iy loop
	} // end iz loop
#ifdef HPCG_DETAILED_DEBUG
	HPCG_fout 	  << "Process " << geom.rank << " of " << geom.size <<" has " << localNumberOfRows    << " rows."     << endl
			  << "Process " << geom.rank << " of " << geom.size <<" has " << localNumberOfNonzeros<< " nonzeros." <<endl;
#endif

  global_int_t totalNumberOfNonzeros = 0;
#ifndef HPCG_NOMPI
  // Use MPI's reduce function to sum all nonzeros
#ifdef HPCG_NO_LONG_LONG
  MPI_Allreduce(&localNumberOfNonzeros, &totalNumberOfNonzeros, 1, MPI_INT, MPI_SUM, MPI_COMM_WORLD);
#else
  long long lnnz = localNumberOfNonzeros, gnnz = 0; // convert to 64 bit for MPI call
  MPI_Allreduce(&lnnz, &gnnz, 1, MPI_LONG_LONG_INT, MPI_SUM, MPI_COMM_WORLD);
  totalNumberOfNonzeros = gnnz; // Copy back
#endif
#else
  totalNumberOfNonzeros = localNumberOfNonzeros;
#endif
        // If this assert fails, it most likely means that the global_int_t is set to int and should be set to long long
        // This assert is usually the first to fail as problem size increases beyond the 32-bit integer range.
        assert(totalNumberOfNonzeros>0); // Throw an exception of the number of nonzeros is less than zero (can happen if int overflow)

	A.title = 0;
	A.totalNumberOfRows = totalNumberOfRows;
	A.totalNumberOfNonzeros = totalNumberOfNonzeros;
	A.localNumberOfRows = localNumberOfRows;
	A.localNumberOfColumns = localNumberOfRows;
	A.localNumberOfNonzeros = localNumberOfNonzeros;
	A.nonzerosInRow = nonzerosInRow;
	A.mtxIndG = mtxIndG;
	A.mtxIndL = mtxIndL;
	A.matrixValues = matrixValues;
	A.matrixDiagonal = matrixDiagonal;

	return;
}
