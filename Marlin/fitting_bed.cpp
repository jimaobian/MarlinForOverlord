#include "Marlin.h"
#include "fitting_bed.h"
#ifdef SoftwareAutoLevel


static void Inverse(float matrix1[][3],float matrix2[][3],int n,float d);
static float Determinant(float matrix[][3],int n);
static float AlCo(float matrix[][3],int jie,int row,int column);
static float Cofactor(float matrix[][3],int jie,int row,int column);

float plainFactorA=0.0, plainFactorB=0.0, plainFactorC=-1.0/ADDING_Z_FOR_POSITIVE;
float plainFactorABackUp=0.0, plainFactorBBackUp=0.0, plainFactorCBackUp=-1.0/ADDING_Z_FOR_POSITIVE;

float plainFactorAAC=0.0;
float plainFactorBBC=0.0;
float plainFactorCAC=1.0;
float plainFactorCBC=1.0;

float fittingBedOffset[NodeNum];
void fittingBedOffsetInit()
{
    fittingBedOffset[0]=0.0;
    fittingBedOffset[1]=0.0;
    fittingBedOffset[2]=0.0;
    fittingBedOffset[3]=0.0;
    fittingBedOffset[4]=0.0;
    fittingBedOffset[5]=-0.3;
//    fittingBedOffset[6]=0.0;
//    fittingBedOffset[7]=0.0;
}


float fittingBedArray[NodeNum][3];

void fittingBedArrayInit()
{
  fittingBedArray[0][X_AXIS]=0;
  fittingBedArray[0][Y_AXIS]=60;
  fittingBedArray[0][Z_AXIS]=ADDING_Z_FOR_POSITIVE;
  
  fittingBedArray[1][X_AXIS]=-52;
  fittingBedArray[1][Y_AXIS]=30;
  fittingBedArray[1][Z_AXIS]=ADDING_Z_FOR_POSITIVE;
  
  fittingBedArray[2][X_AXIS]=-52;
  fittingBedArray[2][Y_AXIS]=-30;
  fittingBedArray[2][Z_AXIS]=ADDING_Z_FOR_POSITIVE;
  
  fittingBedArray[3][X_AXIS]=0;
  fittingBedArray[3][Y_AXIS]=-60;
  fittingBedArray[3][Z_AXIS]=ADDING_Z_FOR_POSITIVE;
  
  fittingBedArray[4][X_AXIS]=52;
  fittingBedArray[4][Y_AXIS]=-30;
  fittingBedArray[4][Z_AXIS]=ADDING_Z_FOR_POSITIVE;
  
  fittingBedArray[5][X_AXIS]=52;
  fittingBedArray[5][Y_AXIS]=30;
  fittingBedArray[5][Z_AXIS]=ADDING_Z_FOR_POSITIVE;
  
//  fittingBedArray[6][X_AXIS]=0;
//  fittingBedArray[6][Y_AXIS]=-60;
//  fittingBedArray[6][Z_AXIS]=ADDING_Z_FOR_POSITIVE;
//  
//  fittingBedArray[7][X_AXIS]=42;
//  fittingBedArray[7][Y_AXIS]=-42;
//  fittingBedArray[7][Z_AXIS]=ADDING_Z_FOR_POSITIVE;
}

void fittingBedUpdateK()
{
  float ac = sqrt(plainFactorA*plainFactorA+plainFactorC*plainFactorC);
  float bc = sqrt(plainFactorB*plainFactorB+plainFactorC*plainFactorC);
  
  plainFactorAAC = -plainFactorA/ac;
  plainFactorBBC = plainFactorB/bc;
  plainFactorCAC = -plainFactorC/ac;
  plainFactorCBC = -plainFactorC/bc;
}

void fittingBedResetK()
{
  plainFactorAAC = 0.0;
  plainFactorBBC = 0.0;
  plainFactorCAC = 1.0;
  plainFactorCBC = 1.0;
}

void fittingBedReset()
{
  plainFactorA=0.0;
  plainFactorB=0.0;
  plainFactorC=-1.0/ADDING_Z_FOR_POSITIVE;
}

void fittingBedResetBackUp()
{
    plainFactorABackUp=0.0;
    plainFactorBBackUp=0.0;
    plainFactorCBackUp=-1.0/ADDING_Z_FOR_POSITIVE;
}

// Ax+By+Cz+1=0


bool fittingBedRaw()
{
  float Y[3];
  
  float Matrix[3][3];
  float IMatrix[3][3];
  
  plainFactorA = plainFactorB = plainFactorC = 0.0;
  memset(Y, 0, sizeof(Y));
  memset(Matrix, 0, sizeof(Matrix));

//  float *Matrix[3],*IMatrix[3];
//  for (int i = 0;i < 3;i++)
//    {
//    Matrix[i]  = MatrixNew[i];
//    IMatrix[i] = new float[3];
//    }
//  for (int i = 0;i < 3;i++)
//    {
//    for (int j = 0;j < 3;j++)
//      {
//      *(Matrix[i] + j) = 0.0;
//      }
//    }
  
  
  for (int j = 0;j < 3;j++)
    {
    for (int i = 0;i < NodeNum;i++)
      {
        Matrix[0][j] += fittingBedArray[i][0]*fittingBedArray[i][j];
        Matrix[1][j] += fittingBedArray[i][1]*fittingBedArray[i][j];
        Matrix[2][j] += fittingBedArray[i][2]*fittingBedArray[i][j];
        Y[j] -= fittingBedArray[i][j];
      }
    }
  float d = Determinant(Matrix,3);
  if (fabs(d) < 0.0001)
    {
    SERIAL_DEBUGLNPGM("singular matrix");
      fittingBedResetK();
      fittingBedReset();
      fittingBedResetBackUp();
    return -1;
    }
  Inverse(Matrix,IMatrix,3,d);
  for (int i = 0;i < 3;i++)
    {
    plainFactorA += *(IMatrix[0] + i)*Y[i];
    plainFactorB += *(IMatrix[1] + i)*Y[i];
    plainFactorC += *(IMatrix[2] + i)*Y[i];
    }
  
  return 0;
}

bool fittingBed()
{
  fittingBedRaw();
  
//  SERIAL_DEBUGLNPGM("fitting error:");
//  
//  float fittingMaxError=0.0;
//  float fittingMinError=0.0;
//  float fittingError=0.0;
//
//  uint8_t fittingMaxErrorIndex;
//  uint8_t fittingMinErrorIndex;
//  
//  for (uint8_t index=0; index<NodeNum; index++) {
//    
//    fittingError=plainFactorA*fittingBedArray[index][X_AXIS]+plainFactorB*fittingBedArray[index][Y_AXIS]+plainFactorC*fittingBedArray[index][Z_AXIS]+1;
//    
//    if (fittingError>fittingMaxError) {
//      fittingMaxError=fittingError;
//      fittingMaxErrorIndex=index;
//    }
//    else if (fittingError<fittingMinError){
//      fittingMinError=fittingError;
//      fittingMinErrorIndex=index;
//    }
//    SERIAL_DEBUGLN(fittingError*1000000);
//  }
//  
//  fittingBedArray[fittingMaxErrorIndex][Z_AXIS]=(-1.0-plainFactorA*fittingBedArray[fittingMaxErrorIndex][X_AXIS]-plainFactorB*fittingBedArray[fittingMaxErrorIndex][Y_AXIS])/plainFactorC;
//  
//  fittingBedArray[fittingMinErrorIndex][Z_AXIS]=(-1.0-plainFactorA*fittingBedArray[fittingMinErrorIndex][X_AXIS]-plainFactorB*fittingBedArray[fittingMinErrorIndex][Y_AXIS])/plainFactorC;
//  
//  fittingBedRaw();
//  
    plainFactorABackUp=plainFactorA;
    plainFactorBBackUp=plainFactorB;
    plainFactorCBackUp=plainFactorC;

//  SERIAL_DEBUGLNPGM("fitting error refinded:");
  
//  for (uint8_t index=0; index<NodeNum; index++) {
//    
//    fittingError=plainFactorA*fittingBedArray[index][X_AXIS]+plainFactorB*fittingBedArray[index][Y_AXIS]+plainFactorC*fittingBedArray[index][Z_AXIS]+1;
//    
//    SERIAL_DEBUGLN(fittingError*1000000);
//  }

    fittingBedArrayInit();

    for (uint8_t index=0; index<NodeNum; index++) {
        fittingBedArray[index][Z_AXIS]=(-1.0-plainFactorA*fittingBedArray[index][X_AXIS]-plainFactorB*fittingBedArray[index][Y_AXIS])/plainFactorC;
        fittingBedArray[index][Z_AXIS]+=fittingBedOffset[index];
    }
    
    fittingBedRaw();
}


void Inverse(float matrix1[][3],float matrix2[][3],int n,float d)
{
  int i,j;
  
  for(i=0;i<n;i++)
    for(j=0;j<n;j++)
      matrix2[j][i]=(AlCo(matrix1,n,i,j)/d);
}

float Determinant(float matrix[][3],int n)
{
  float result=0,temp;
  int i;
  if(n==1)
    result=(matrix[0][0]);
  else
    {
    for(i=0;i<n;i++)
      {
      temp=AlCo(matrix,n,n-1,i);
      result+=(matrix[n-1][i])*temp;
      }
    }
  return result;
}

float AlCo(float matrix[][3],int jie,int row,int column)
{
  float result;
  if((row+column)%2 == 0)
    result = Cofactor(matrix,jie,row,column);
  else
    result=(-1)*Cofactor(matrix,jie,row,column);
  return result;
}

float Cofactor(float matrix[][3],int jie,int row,int column)
{
  float result;
  int i,j;
  float smallmatr[3][3];
  
  for(i=0;i<row;i++)
    for(j=0;j<column;j++)
      smallmatr[i][j]=matrix[i][j];
  for(i=row;i<jie-1;i++)
    for(j=0;j<column;j++)
      smallmatr[i][j]=matrix[i+1][j];
  for(i=0;i<row;i++)
    for(j=column;j<jie-1;j++)
      smallmatr[i][j]=matrix[i][j+1];
  for(i=row;i<jie-1;i++)
    for(j=column;j<jie-1;j++)
      smallmatr[i][j]=matrix[i+1][j+1];
  result = Determinant(smallmatr,jie-1);
  
  //    SERIAL_DEBUGLNPGM("free memory");
  //    SERIAL_DEBUGLN(freeRam());

  return result;
}


#endif