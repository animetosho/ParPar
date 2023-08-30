#include "reedsolomon.h"
#include <cstring>
using namespace std;


static u32 gcd(u32 a, u32 b)
{
  if (a && b)
  {
    while (a && b)
    {
      if (a>b)
      {
        a = a%b;
      }
      else
      {
        b = b%a;
      }
    }

    return a+b;
  }
  else
  {
    return 0;
  }
}


inline bool ReedSolomon_GaussElim(unsigned int rows, unsigned int leftcols, Galois16 *leftmatrix, Galois16 *rightmatrix, unsigned int datamissing)
{
  // Because the matrices being operated on are Vandermonde matrices
  // they are guaranteed not to be singular.

  // Additionally, because Galois arithmetic is being used, all calculations
  // involve exact values with no loss of precision. It is therefore
  // not necessary to carry out any row or column swapping.

  // Solve one row at a time

  // For each row in the matrix
  for (unsigned int row=0; row<datamissing; row++)
  {
    // NB Row and column swapping to find a non zero pivot value or to find the largest value
    // is not necessary due to the nature of the arithmetic and construction of the RS matrix.

    // Get the pivot value.
    Galois16 pivotvalue = rightmatrix[row * rows + row];
    if (pivotvalue == 0)
    {
      return false;
    }

    // If the pivot value is not 1, then the whole row has to be scaled
    if (pivotvalue != 1)
    {
      for (unsigned int col=0; col<leftcols; col++)
      {
        if (leftmatrix[row * leftcols + col] != 0)
        {
          leftmatrix[row * leftcols + col] /= pivotvalue;
        }
      }
      rightmatrix[row * rows + row] = 1;
      for (unsigned int col=row+1; col<rows; col++)
      {
        if (rightmatrix[row * rows + col] != 0)
        {
          rightmatrix[row * rows + col] /= pivotvalue;
        }
      }
    }

    // For every other row in the matrix
    for (unsigned int row2=0; row2<rows; row2++)
    {
      // Define MPDL to skip reporting and speed things up

      if (row != row2)
      {
        // Get the scaling factor for this row.
        Galois16 scalevalue = rightmatrix[row2 * rows + row];

        if (scalevalue == 1)
        {
          // If the scaling factor happens to be 1, just subtract rows
          for (unsigned int col=0; col<leftcols; col++)
          {
            if (leftmatrix[row * leftcols + col] != 0)
            {
              leftmatrix[row2 * leftcols + col] -= leftmatrix[row * leftcols + col];
            }
          }

          for (unsigned int col=row; col<rows; col++)
          {
            if (rightmatrix[row * rows + col] != 0)
            {
              rightmatrix[row2 * rows + col] -= rightmatrix[row * rows + col];
            }
          }
        }
        else if (scalevalue != 0)
        {
          // If the scaling factor is not 0, then compute accordingly.
          for (unsigned int col=0; col<leftcols; col++)
          {
            if (leftmatrix[row * leftcols + col] != 0)
            {
              leftmatrix[row2 * leftcols + col] -= leftmatrix[row * leftcols + col] * scalevalue;
            }
          }

          for (unsigned int col=row; col<rows; col++)
          {
            if (rightmatrix[row * rows + col] != 0)
            {
              rightmatrix[row2 * rows + col] -= rightmatrix[row * rows + col] * scalevalue;
            }
          }
        }
      }
    }
  }

  return true;
}



// Construct the Vandermonde matrix and solve it if necessary
bool ReedSolomon_Compute(const vector<bool> &present, vector<RSOutputRow> outputrows, Galois16*& leftmatrix)
{
  // SetInput
  u32 inputcount = (u32)present.size();

  vector<u32> datapresentindex(inputcount);
  vector<u32> datamissingindex(inputcount);
  vector<Galois16::ValueType> database(inputcount);
  u32 datapresent = 0, datamissing = 0;

  unsigned int logbase = 0;

  for (unsigned int index=0; index<inputcount; index++)
  {
    // Record the index of the file in the datapresentindex array
    // or the datamissingindex array
    if (present[index])
    {
      datapresentindex[datapresent++] = index;
    }
    else
    {
      datamissingindex[datamissing++] = index;
    }

    // Determine the next useable base value.
    // Its log must must be relatively prime to 65535
    while (gcd(Galois16::Limit, logbase) != 1)
    {
      logbase++;
    }
    if (logbase >= Galois16::Limit)
    {
      return false;
    }
    Galois16::ValueType base = Galois16(logbase++).ALog();

    database[index] = base;
  }
  
  
  
  
  // Compute
  u32 outcount = datamissing;
  u32 incount = datapresent + datamissing;

  if (datamissing > outputrows.size()) return false;
  if (outcount == 0)
  {
    return false;
  }

  // Allocate the left hand matrix

  leftmatrix = new Galois16[outcount * incount];
  for (unsigned int index=0; index < outcount * incount; index++)
    leftmatrix[index] = 0;

  // Allocate the right hand matrix only if we are recovering

  Galois16 *rightmatrix = 0;
  if (datamissing > 0)
  {
    rightmatrix = new Galois16[outcount * outcount];
    for (unsigned int index=0; index < outcount * outcount; index++)
      rightmatrix[index] = 0;
  }

  // Fill in the two matrices:

  vector<RSOutputRow>::const_iterator outputrow = outputrows.begin();

  // One row for each present recovery block that will be used for a missing data block
  for (unsigned int row=0; row<datamissing; row++)
  {
    // Get the exponent of the next present recovery block
    while (!outputrow->present)
    {
      outputrow++;
    }
    u16 exponent = outputrow->exponent;

    // One column for each present data block
    for (unsigned int col=0; col<datapresent; col++)
    {
      leftmatrix[row * incount + col] = Galois16(database[datapresentindex[col]]).pow(exponent);
    }
    // One column for each each present recovery block that will be used for a missing data block
    for (unsigned int col=0; col<datamissing; col++)
    {
      leftmatrix[row * incount + col + datapresent] = (row == col) ? 1 : 0;
    }

    if (datamissing > 0)
    {
      // One column for each missing data block
      for (unsigned int col=0; col<datamissing; col++)
      {
        rightmatrix[row * outcount + col] = Galois16(database[datamissingindex[col]]).pow(exponent);
      }
    }

    outputrow++;
  }

  // Solve the matrices only if recovering data
  if (datamissing > 0)
  {
    // Perform Gaussian Elimination and then delete the right matrix (which
    // will no longer be required).
    bool success = ReedSolomon_GaussElim(outcount, incount, leftmatrix, rightmatrix, datamissing);
    delete [] rightmatrix;
    return success;
  }

  return true;
}

// Use Gaussian Elimination to solve the matrices

