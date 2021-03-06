/******************************************************************************
 *  HELP (Homogeneous and self-dual Efficient Linear Programming)             *
 *                                                                            *
 *  An experimental implementation of Homogeneous and Self-Dual Algorithm     *
 *  for Linear Programming.                                                   *
 *                                                                            *
 *  Author: Tian Xie (Research Center for Management Science and Information  *
 *          Analytics, Shanghai University of Finance and Economics)          *
 *                                                                            *
 *  Credits: Fundamental implementation idea originated from COPL_LP.         *
 *           (Xiong Zhang and Yinyu Ye)                                       *
 *           See http://web.stanford.edu/~yyye/Col.html .                     *
 ******************************************************************************/

#include "LP.h"
#include "mkl.h"
#include "mkl_types.h"
#include "mkl_spblas.h"
#include "mkl_pardiso.h"

const char CHAR_T = 'T';
const char CHAR_N = 'N';
const double DOUBLE_ONE = 1.0;
const double DOUBLE_NEGONE = -1.0;
const int INT_ZERO = 0;
const int INT_ONE = 1;
const int INT_TWO = 2;
const int INT_SEVEN = 7;

void* PARDISO_pt[64];
int PARDISO_iparm[64];
int PARDISO_maxfct, PARDISO_mnum, PARDISO_mtype, PARDISO_phase, PARDISO_n, PARDISO_nrhs, PARDISO_msglvl, PARDISO_error;
int INT_dummy;
double DOUBLE_dummy;

int nnzA; 
double *csrValAt; int csrRowPtrAt[MAX_COLS + 1]; int *csrColIndAt;
double *csrValDAt;
int Perm[MAX_ROWS];
int max_nnzADAt, nnzADAt; double *csrValADAt; int csrRowPtrADAt[MAX_ROWS + 1]; int *csrColIndADAt;
int max_nnzTriADAt, nnzTriADAt; double *csrValTriADAt; int csrRowPtrTriADAt[MAX_ROWS + 1]; int *csrColIndTriADAt;

double tmp_row[MAX_ROWS], tmp_col[MAX_COLS];
double X1[MAX_COLS], X2[MAX_ROWS];

int LinearEquation_Construct()
{
	//mkl_set_num_threads(4);

	// PARDISO Control Parameters
	// https://software.intel.com/zh-cn/node/470284#E44B4021-701A-48DA-BA29-70CFA20766AA
	// https://software.intel.com/zh-cn/node/470296#0EADD82B-06F8-4262-8C98-02775C054ABE

#ifdef PRINT_DEBUG
	printf("A = zeros(%d, %d);\n", n_Row, n_Col);
	for (int j = 0; j < n_Col; j ++)
		for (int p = V_Matrix_Col_Head[j]; p != -1; p = V_Matrix_Col_Next[p])
			printf("A(%d, %d) = %lf;\n", V_Matrix_Row[p] + 1, j + 1, V_Matrix_Value[p]);
#endif

#ifdef PRINT_TIME
	double Tm = GetTime();
#endif
	// Count A size
	nnzA = 0;
	for (int j = 0; j < n_Col; j ++)
		for (int p = V_Matrix_Col_Head[j]; p != -1; p = V_Matrix_Col_Next[p])
			nnzA ++;
	csrValAt = (double*) malloc(sizeof(double) * nnzA);
	csrColIndAt = (int*) malloc(sizeof(double) * nnzA);
	csrValDAt = (double*) malloc(sizeof(double) * nnzA);

	// Construct matrix At in CSR format. 
	// After Presolve_Init(), we can assume A is sorted.
	nnzA = 0;
	for (int j = 0; j < n_Col; j ++)
	{
		csrRowPtrAt[j] = nnzA + 1; // One-based
		for (int p = V_Matrix_Col_Head[j]; p != -1; p = V_Matrix_Col_Next[p])
		{
			csrValAt[nnzA] = V_Matrix_Value[p];
			csrColIndAt[nnzA] = V_Matrix_Row[p] + 1; // One-based
			nnzA ++;
		}
	}
	csrRowPtrAt[n_Col] = nnzA + 1;

	// Symbolic Analysis: METIS, Nested Dissection Algorithm
	// Make a symbolic matrix
#ifdef DEBUG_TRACK
	printf("Before Test Multiplication\n");
#endif
	for (int i = 0; i < nnzA; i ++)
		csrValDAt[i] = 1.0;
	int MKL_Error;
	mkl_dcsrmultcsr(&CHAR_T, &INT_ONE, &INT_SEVEN, &n_Col, &n_Row, &n_Row, csrValDAt, csrColIndAt, csrRowPtrAt, 
		csrValDAt, csrColIndAt, csrRowPtrAt, &DOUBLE_dummy, &INT_dummy, csrRowPtrADAt, &INT_dummy, &MKL_Error);
	max_nnzADAt = csrRowPtrADAt[n_Row] - 1;
	csrValADAt = (double*) malloc(sizeof(double) * max_nnzADAt);
	csrColIndADAt = (int*) malloc(sizeof(int) * max_nnzADAt);
	mkl_dcsrmultcsr(&CHAR_T, &INT_TWO, &INT_SEVEN, &n_Col, &n_Row, &n_Row, csrValDAt, csrColIndAt, csrRowPtrAt, 
		csrValDAt, csrColIndAt, csrRowPtrAt, csrValADAt, csrColIndADAt, csrRowPtrADAt, &max_nnzADAt, &MKL_Error);
	// Extract Upper Triangle of ADAt
	max_nnzTriADAt = 0;  // Only Upper Triangle
	for (int i = 0; i < n_Row; i ++)
		for (int p = csrRowPtrADAt[i] - 1; p < csrRowPtrADAt[i + 1] - 1; p ++)
		{
			int j = csrColIndADAt[p] - 1;
			if (i <= j)
				max_nnzTriADAt ++;
		}
	csrValTriADAt = (double*) malloc(sizeof(double) * max_nnzTriADAt);
	csrColIndTriADAt = (int*) malloc(sizeof(int) * max_nnzTriADAt);
	nnzTriADAt = 0;
	for (int i = 0; i < n_Row; i ++)
	{
		csrRowPtrTriADAt[i] = nnzTriADAt + 1;
		for (int p = csrRowPtrADAt[i] - 1; p < csrRowPtrADAt[i + 1] - 1; p ++)
		{
			int j = csrColIndADAt[p] - 1;
			if (i <= j)
			{
				csrValTriADAt[nnzTriADAt] = csrValADAt[p];
				csrColIndTriADAt[nnzTriADAt] = j + 1;
				nnzTriADAt ++;
			}
		}
	}
	csrRowPtrTriADAt[n_Row] = nnzTriADAt + 1;
#ifdef DEBUG_TRACK
	printf("After Test Multiplication\n");
#endif
#ifdef PRINT_TIME
	printf("Test Multiplication: %.2lf s\n", GetTime() - Tm);
#endif

	// Initialize PARDISO parameters
	memset(PARDISO_pt, 0, sizeof(void*) * 64);
	memset(PARDISO_iparm, 0, sizeof(int) * 64);
	PARDISO_iparm[0] = 1; // Not default
	PARDISO_iparm[1] = 3; // OpenMP version of the nested dissection algorithm
	PARDISO_iparm[3] = 0; // Direct algorithm
	PARDISO_iparm[4] = 2; // Output Perm
	PARDISO_iparm[5] = 0; // Write solution into x
	PARDISO_iparm[7] = 0; // Two steps of iterative refinement
	PARDISO_iparm[9] = 13; // Pivoting pertubation: eps = 1e-13
	PARDISO_iparm[10] = 1; // Enable scaling
	PARDISO_iparm[11] = 0; // Solve AX = b
	PARDISO_iparm[12] = 0; // Disable maximum weighted matching
	PARDISO_iparm[17] = 0; // Disable reporting non-zero elements
	PARDISO_iparm[18] = 0; // Disable reporting floating point operations (in million)
	PARDISO_iparm[23] = 1; // Two-level parallel factorization algorithm
	PARDISO_iparm[24] = 0; // Parallel algorithm for the solve step
	PARDISO_iparm[26] = 0; // Disable matrix checker
	PARDISO_iparm[27] = 0; // Double precision
	PARDISO_iparm[30] = 0; // No partial solve / computing
	PARDISO_iparm[33] = 0; // CNR mode: Default
	PARDISO_iparm[34] = 0; // ONE-based indexing
	PARDISO_iparm[35] = 0; // Do not compute Schur complement
	PARDISO_iparm[36] = 0; // CSR format
	PARDISO_iparm[55] = 0; // Diagonal and pivoting control: Default
	PARDISO_iparm[59] = 0; // PARDISO mode: IC (in-core)
	PARDISO_maxfct = 1;
	PARDISO_mnum = 1;
	PARDISO_mtype = 2;
	PARDISO_phase = 11;
	PARDISO_n = n_Row;
	PARDISO_nrhs = 1;
	PARDISO_msglvl = 0;
	
	// Always, DO NOT FORGET Perm!
#ifdef PRINT_TIME
	Tm = GetTime();
#endif
#ifdef DEBUG_TRACK
	printf("Before Symbolic Nested Dissection\n");
#endif
	PARDISO(PARDISO_pt, &PARDISO_maxfct, &PARDISO_mnum, &PARDISO_mtype, &PARDISO_phase, &PARDISO_n, csrValTriADAt, csrRowPtrTriADAt, csrColIndTriADAt, 
		Perm, &PARDISO_nrhs, PARDISO_iparm, &PARDISO_msglvl, &DOUBLE_dummy, &DOUBLE_dummy, &PARDISO_error);
	CheckError(PARDISO_error, "PARDISO Analysis Error!");
#ifdef DEBUG_TRACK
	printf("After Symbolic Nested Dissection\n");
#endif
#ifdef PRINT_TIME
	printf("Nested Dissection: %.2lf s\n", GetTime() - Tm);
#endif
	PARDISO_iparm[4] = 1; // Input Perm
	return 0;
}

int LinearEquation_Destruct()
{
	if (csrValAt) { free(csrValAt); csrValAt = 0; }
	if (csrColIndAt) { free(csrColIndAt); csrColIndAt = 0; }
	if (csrValDAt) { free(csrValDAt); csrValDAt = 0; }
	if (csrValADAt) { free(csrValADAt); csrValADAt = 0; }
	if (csrColIndADAt) { free(csrColIndADAt); csrColIndADAt = 0; }
	if (csrValTriADAt) { free(csrValTriADAt); csrValTriADAt = 0; }
	if (csrColIndTriADAt) { free(csrColIndTriADAt); csrColIndTriADAt = 0; }
	PARDISO_phase = -1;
	PARDISO(PARDISO_pt, &PARDISO_maxfct, &PARDISO_mnum, &PARDISO_mtype, &PARDISO_phase, &PARDISO_n, &DOUBLE_dummy, &INT_dummy, &INT_dummy, 
		&INT_dummy, &INT_dummy, PARDISO_iparm, &PARDISO_msglvl, &DOUBLE_dummy, &DOUBLE_dummy, &PARDISO_error);
	return 0;
}

// Renew ADA^T and factorize
void RenewLinearEquation(double* d) // d should be inversed
{
#ifdef PRINT_TIME
	double Tm = GetTime();
#endif
	// Calc DAt
	for (int j = 0; j < n_Col; j ++)
	{
		double dinv_j = 1.0 / d[j];
		for (int p = csrRowPtrAt[j] - 1; p < csrRowPtrAt[j + 1] - 1; p ++)
			csrValDAt[p] = csrValAt[p] * dinv_j;
	}
	// Calc ADAt
	int MKL_Error;
	mkl_dcsrmultcsr(&CHAR_T, &INT_TWO, &INT_SEVEN, &n_Col, &n_Row, &n_Row, csrValAt, csrColIndAt, csrRowPtrAt, 
		csrValDAt, csrColIndAt, csrRowPtrAt, csrValADAt, csrColIndADAt, csrRowPtrADAt, &max_nnzADAt, &MKL_Error);
#ifdef PRINT_TIME
	printf("%%Calc ADAt: %.2lf s\n", GetTime() - Tm);
#endif
#ifdef PRINT_TIME
	Tm = GetTime();
#endif
	// Extract Upper Triangle of ADAt
	nnzTriADAt = 0;
#ifdef PRINT_DEBUG
	printf("ADAt = zeros(%d, %d);\n", n_Row, n_Row);
#endif
	for (int i = 0; i < n_Row; i ++)
	{
		csrRowPtrTriADAt[i] = nnzTriADAt + 1;
		for (int p = csrRowPtrADAt[i] - 1; p < csrRowPtrADAt[i + 1] - 1; p ++)
		{
			int j = csrColIndADAt[p] - 1;
			if (i <= j)
			{
				csrValTriADAt[nnzTriADAt] = csrValADAt[p];
				csrColIndTriADAt[nnzTriADAt] = j + 1;
				nnzTriADAt ++;
			}
#ifdef PRINT_DEBUG
			printf("ADAt(%d, %d) = %lf;\n", i + 1, j + 1, csrValADAt[p]);
#endif
		}
	}
	csrRowPtrTriADAt[n_Row] = nnzTriADAt + 1;
#ifdef PRINT_TIME
	printf("%%Extract Upper Triangle of ADAt: %.2lf s\n", GetTime() - Tm);
#endif
#ifdef PRINT_TIME
	Tm = GetTime();
#endif

/*
#ifdef DEBUG_TRACK
	printf("%%Before Analysis (Given Perm)\n");
#endif
	PARDISO_phase = 11;
	PARDISO(PARDISO_pt, &PARDISO_maxfct, &PARDISO_mnum, &PARDISO_mtype, &PARDISO_phase, &PARDISO_n, csrValTriADAt, csrRowPtrTriADAt, csrColIndTriADAt, 
		Perm, &PARDISO_nrhs, PARDISO_iparm, &PARDISO_msglvl, &DOUBLE_dummy, &DOUBLE_dummy, &PARDISO_error);
	CheckError(PARDISO_error, "PARDISO Analysis (Given Perm) Error!");
#ifdef DEBUG_TRACK
	printf("%%After Analysis (Given Perm)\n");
#endif
#ifdef PRINT_TIME
	printf("%%Analysis (Given Perm): %.2lf s\n", GetTime() - Tm);
#endif
*/

#ifdef PRINT_TIME
	Tm = GetTime();
#endif
#ifdef DEBUG_TRACK
	printf("%%Before Factorize\n");
#endif
	PARDISO_phase = 22;
	PARDISO(PARDISO_pt, &PARDISO_maxfct, &PARDISO_mnum, &PARDISO_mtype, &PARDISO_phase, &PARDISO_n, csrValTriADAt, csrRowPtrTriADAt, csrColIndTriADAt, 
		Perm, &PARDISO_nrhs, PARDISO_iparm, &PARDISO_msglvl, &DOUBLE_dummy, &DOUBLE_dummy, &PARDISO_error);
	CheckError(PARDISO_error, "PARDISO Factorize Error!");
#ifdef DEBUG_TRACK
	printf("%%After Factorize\n");
#endif
#ifdef PRINT_TIME
	printf("%%Factorize: %.2lf s\n", GetTime() - Tm);
#endif
}

// Solve [ D  A^T ][x_1] == [b_1]
//       [ A   0  ][x_2]    [b_2]
// A: m * n; D: Diagonal Matrix, n * n; x_1: n * 1; x_2: m * 1; b_1: n * 1; b_2: m * 1
// Solution: (A D^(-1) A^T) x_2 = (A D^(-1) b_1 - b_2)
//           x_1 = D^(-1) (b_1 - A^T x_2)
// Need to call RenewLinearEquation(d) if ADA^T is not factorized yet!
int SolveLinearEquation(double* d, double* b_1, double* b_2, double* x_1, double* x_2)
{
#ifdef PRINT_DEBUG
	printf("b_1 = zeros(%d, 1);\n", n_Col);
	for (int i = 0; i < n_Col; i ++)
		printf("b_1(%d) = %.6lf;\n", i + 1, b_1[i]);
	printf("b_2 = zeros(%d, 1);\n", n_Row);
	for (int i = 0; i < n_Row; i ++)
		printf("b_2(%d) = %.6lf;\n", i + 1, b_2[i]);
#endif
	for (int i = 0; i < n_Row; i ++)
		tmp_row[i] = -b_2[i];
	// D^(-1) b_1
	for (int i = 0; i < n_Col; i ++)
		tmp_col[i] = b_1[i] / d[i];
	// A * (D^(-1) b_1) - b_2
	char MKL_matdescra[6];
	MKL_matdescra[0] = 'G';
	MKL_matdescra[3] = 'F';
	mkl_dcsrmv(&CHAR_T, &n_Col, &n_Row, &DOUBLE_ONE, MKL_matdescra, csrValAt, csrColIndAt, csrRowPtrAt, csrRowPtrAt + 1, tmp_col, &DOUBLE_ONE, tmp_row);

	// Solve (A D^(-1) A^T) x_2 = (A D^(-1) b_1 - b_2)
#ifdef PRINT_TIME
	double Tm = GetTime();
#endif
#ifdef DEBUG_TRACK
printf("%%Before Solve\n");
#endif
	PARDISO_phase = 33;
	PARDISO(PARDISO_pt, &PARDISO_maxfct, &PARDISO_mnum, &PARDISO_mtype, &PARDISO_phase, &PARDISO_n, csrValTriADAt, csrRowPtrTriADAt, csrColIndTriADAt, 
		Perm, &PARDISO_nrhs, PARDISO_iparm, &PARDISO_msglvl, tmp_row, X2, &PARDISO_error);
	CheckError(PARDISO_error, "PARDISO Solve Error!");
#ifdef DEBUG_TRACK
printf("%%After Solve\n");
#endif
#ifdef PRINT_TIME
	printf("%%Solve: %.2lf s\n", GetTime() - Tm);
#endif
	for (int i = 0; i < n_Row; i ++)
		x_2[i] = X2[i];
#ifdef PRINT_DEBUG
	for (int i = 0; i < n_Row; i ++)
		printf("x_2(%d) = %lf\n", i + 1, x_2[i]);
#endif
	// b_1 - A^T x_2
	for (int i = 0; i < n_Col; i ++)
		tmp_col[i] = b_1[i];
	mkl_dcsrmv(&CHAR_N, &n_Col, &n_Row, &DOUBLE_NEGONE, MKL_matdescra, csrValAt, csrColIndAt, csrRowPtrAt, csrRowPtrAt + 1, x_2, &DOUBLE_ONE, tmp_col);
	// Let x_1 = D^(-1) (b_1 - A^T x_2)
	for (int i = 0; i < n_Col; i ++)
		x_1[i] = tmp_col[i] / d[i];
#ifdef PRINT_DEBUG
	for (int i = 0; i < n_Col; i ++)
		printf("x_1(%d) = %lf\n", i + 1, x_1[i]);
#endif
	return 0;
}

