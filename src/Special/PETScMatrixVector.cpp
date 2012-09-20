/*
 * =====================================================================================
 *
 *       Filename: PETScMatrixVector.cpp
 *
 *    Description: Matrix-Scalar multiplication helper for PETSc
 *
 *         Author: Paul P. Hilscher (2012-), 
 *
 *        License: GPLv3+
 * =====================================================================================
 */

#include "Fields.h"

#include "PETScMatrixVector.h"

Vlasov *GL_vlasov;
Fields *GL_fields;

int GL_iter;


int petc_signal_handler(int sig, void *ctx) {
    // hard exit ( try to improve using control)
    check(-1, DMESG("PETSc signal received"));

    return 0;
}



PETScMatrixVector::PETScMatrixVector(Vlasov *vlasov, Fields *fields)
{

            GL_vlasov = vlasov;
            GL_fields = fields;
            GL_iter   = 0;
};


PetscErrorCode PETScMatrixVector::MatrixVectorProduct(Mat A, Vec Vec_x, Vec Vec_y) 

{

  [=] (CComplex  fs  [NsLD][NmLD ][NzLB][NkyLD][NxLB][NvLB],  
       CComplex  fss [NsLD][NmLD ][NzLB][NkyLD][NxLB][NvLB])
  {
      
    std::cout << "\r"   << "Iteration  : " << GL_iter++ << std::flush;

    CComplex *x_F1, *y_F1; 
 
    VecGetArrayRead(Vec_x, (const PetscScalar **) &x_F1);
    VecGetArray    (Vec_y, (      PetscScalar **) &y_F1);

    // copy whole phase space function (waste but starting point) (important due to bounday conditions
    // we can built wrapper around this and directly pass it
    for(int x = NxLlD, n = 0; x <= NxLuD; x++) { for(int y_k = NkyLlD+1; y_k <= NkyLuD-1; y_k++) { for(int z = NzLlD; z <= NzLuD; z++) {
    for(int v = NvLlD       ; v <= NvLuD; v++) { for(int m   = NmLlD   ; m   <= NmLuD   ; m++  ) { for(int s = NsLlD; s <= NsLuD; s++) {
      
           fs[s][m][z][y_k][x][v] = x_F1[n++];

   }}} }}}

   GL_vlasov->setBoundary(GL_vlasov->fs); 
   GL_fields->solve(GL_vlasov->f0,  GL_vlasov->fs); 
   
   const double rk_0[] = { 0., 0., 0.};
   GL_vlasov->solve(GL_vlasov->getEquationType(), GL_fields, GL_vlasov->fs, GL_vlasov->fss, 1., 0, rk_0);
   
   // copy whole phase space function (waste but starting point) (important due to bounday conditions
   //#pragma omp parallel for, collapse private(n) 
   for(int x = NxLlD, n = 0; x <= NxLuD; x++) { for(int y_k = NkyLlD+1; y_k <= NkyLuD-1; y_k++) { for(int z = NzLlD; z <= NzLuD; z++) {
   for(int v = NvLlD       ; v <= NvLuD; v++) { for(int m   = NmLlD   ; m   <= NmLuD   ; m++  ) { for(int s = NsLlD; s <= NsLuD; s++) {

       y_F1[n++] = fss[s][m][z][y_k][x][v];

   }}} }}}
 
   VecRestoreArrayRead(Vec_x, (const PetscScalar **) &x_F1);
   VecRestoreArray    (Vec_y, (      PetscScalar **) &y_F1);

   } ((A6zz) GL_vlasov->fs.dataZero(), (A6zz) GL_vlasov->fss.dataZero());
   
   return 0; // return 0 (success) required for PETSc
}

Complex* PETScMatrixVector::getCreateVector(Grid *grid, Vec &Vec_x) {

    Complex *xp;

    VecCreateMPI(MPI_COMM_WORLD, grid->getLocalSize(), grid->getGlobalSize(), &Vec_x);
    VecAssemblyBegin(Vec_x);
    VecAssemblyEnd(Vec_x);

    VecGetArray    (Vec_x, &xp);

    return xp;
}
 


