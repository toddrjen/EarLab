#include "MatrixN.h"
#include "Earlab.h"
#include <stdio.h>
#include "Logger.h"
#include "BinaryWriter.h"
#include "EarlabDataStream.h"

class MSOXcorr
{
public:
    MSOXcorr();
    ~MSOXcorr();

    int ReadParameters(char *ParameterFileName);
    int ReadParameters(char *ParameterFileName, char *SectionName);

    int Start(int NumInputs,  EarlabDataStreamType InputTypes[EarlabMaxIOStreamCount],  int InputSize[EarlabMaxIOStreamCount][EarlabMaxIOStreamDimensions], 
			int NumOutputs, EarlabDataStreamType OutputTypes[EarlabMaxIOStreamCount], int OutputSize[EarlabMaxIOStreamCount][EarlabMaxIOStreamDimensions],
			unsigned long OutputElementCounts[EarlabMaxIOStreamCount]);
    int Advance(EarlabDataStream *InputStream[EarlabMaxIOStreamCount], EarlabDataStream *OutputStream[EarlabMaxIOStreamCount]);
    int Stop(void);
    int Unload(void);

	void SetModuleName(char *ModuleName);
	void SetLogger(Logger *TheLogger);

private:
	int HalfCorrelate(double ipsi[], double contra[], long n, double lag[], long shiftmax);
	double ComputeLag(double ipsi[], double contra[], long n);

	Logger *mLogger;
	char *mModuleName;
	char mCFFileName[256], mLagFileName[256];
    int mFrameSize_Samples, mNumChannels, mMaxLag_Samples, mFrameCount;
	double mSampleRate_Hz;
	double mMaxITD_uS;
	double *mIpsilateral, *mContralateral, *mLag;
	double mDeltaT;
	DoubleMatrixN mFrameBuf[2];
	BinaryWriter *mWriter;
};