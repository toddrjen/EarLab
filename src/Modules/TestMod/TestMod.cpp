/******************************************************************/
/*  TestMod.cpp                                           */
/*  Generates simple test signals for EarLab modules              */
/*                                                                */
/*  4 September 2001      a.1    AWH    First Alpha draft         */
/*  8 May 2003            a.2    DJA    Turned gensig.c into      */
/*                                      EarLab module siggen.c    */
/* 29 Jul 2003            a.3    DJA    Turned siggen.c into C++  */
/*                                      module TestMod.cpp*/
/******************************************************************/

#include <iostream>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <time.h>

#include "CParameterFile.h"
#include "CParameterSpace.h"
#include "Earlab.h"
#include "TestMod.h"
#include "EarlabException.h"

using namespace std;

TestMod::TestMod()
{
	memset(this, 0, sizeof(TestMod));
	mLogger = new Logger();
	SetModuleName("TestMod");
}

TestMod::~TestMod()
{
	if (mModuleName != NULL)
		delete [] mModuleName;
}
/******************************************************************/
/*                          GetParameters                         */
/******************************************************************/

int TestMod::ReadParameters(char *ParameterFileName)
{
	if (mModuleName == NULL)
		return ReadParameters(ParameterFileName, "TestMod");
	else
	    return ReadParameters(ParameterFileName, mModuleName);
}

int TestMod::ReadParameters(char *ParameterFileName, char *SectionName)
{
     CParameterSpace theParamFile(ParameterFileName);
    ParameterStatus Status;

	cout << "   ++ReadParameters: " << mModuleName << endl;

    Status = theParamFile.GetParameter(SectionName, "SampleRate_Hz", mSampleRate_Hz, mSampleRate_Hz);

cout << " SampleRate = " << mSampleRate_Hz << endl;

	Status = theParamFile.GetParameter(SectionName, "ProduceSineSignal", mProduceSineSignal, false);
    Status = theParamFile.GetParameter(SectionName, "SineAmplitude_dB", mSineAmplitude_dB, mSineAmplitude_dB);
    Status = theParamFile.GetParameter(SectionName, "SineFrequency_Hz", mSineFrequency_Hz, mSineFrequency_Hz);

	Status = theParamFile.GetParameter(SectionName, "ProduceImpulseSignal", mProduceImpulseSignal, false);
    Status = theParamFile.GetParameter(SectionName, "ImpulseAmplitude_dB", mImpulseAmplitude_dB, mImpulseAmplitude_dB);
    Status = theParamFile.GetParameter(SectionName, "ImpulseFrequency_Hz", mImpulseFrequency_Hz, mImpulseFrequency_Hz);

	Status = theParamFile.GetParameter(SectionName, "ProduceNoiseSignal", mProduceNoiseSignal, false);
    Status = theParamFile.GetParameter(SectionName, "NoiseAmplitude_dB", mNoiseAmplitude_dB, mNoiseAmplitude_dB);

	Status = theParamFile.GetParameter(SectionName, "SineModulateSignal", mSineModulateSignal, false);
    Status = theParamFile.GetParameter(SectionName, "ModulationDepth_Percent", mModulationDepth_Percent, mModulationDepth_Percent);
    Status = theParamFile.GetParameter(SectionName, "ModulationFrequency_Hz", mModulationFrequency_Hz, mModulationFrequency_Hz);

	if (mModulationDepth < 0)   mModulationDepth = 0;
	if (mModulationDepth > 100) mModulationDepth = 100;

	Status = theParamFile.GetParameter(SectionName, "ProduceOutputFile", mProduceOutputFile, false);
	Status = theParamFile.GetParameter(SectionName, "OutputFileName", mOutputFileName, sizeof(mOutputFileName), mOutputFileName);
	return 1;
}

int TestMod::Start(int NumInputs,  EarlabDataStreamType InputTypes[EarlabMaxIOStreamCount],  int InputSize[EarlabMaxIOStreamCount][EarlabMaxIOStreamDimensions],
			int NumOutputs, EarlabDataStreamType OutputTypes[EarlabMaxIOStreamCount], int OutputSize[EarlabMaxIOStreamCount][EarlabMaxIOStreamDimensions],
			unsigned long OutputElementCounts[EarlabMaxIOStreamCount])
{
	// Because this module produces data in a time series format, the following statements apply:
	// OutputSize[i][0] is the number of samples in a frame
	// OutputSize[i][1] must be 0 to mark the end of the dimension count

	time_t now;
	cout << "    Start: " << mModuleName << endl;
	// Perform some validation on my parameters to make sure I can handle the requested input and output streams...
	if (NumInputs != 0)
		throw EarlabException("TestMod: Currently this module cannot handle input streams.  Sorry!");

	if (NumOutputs != 1)
		throw EarlabException("TestMod: Currently this module can only handle one output stream.  Sorry!");

	if (OutputTypes[0] != WaveformData)
		throw EarlabException("%s: Currently this module can only handle waveform output data streams.  Sorry!", mModuleName);

	if (OutputSize[0][1] != 0)
		throw EarlabException("TestMod: Output data must be one-dimensional.  Sorry!");

	OutputElementCounts[0] = OutputSize[0][0];

	time(&now);
	srand((unsigned int)now);

    mSineAmplitude = (float)pow(10.0, mSineAmplitude_dB / 20) * 20;
    mSinePeriod = (float)(mSampleRate_Hz / mSineFrequency_Hz);
    mSineOmegaDelta = (float)(2.0 * PI / mSinePeriod);
    mSineOmega = 0;

	mImpulseAmplitude = (float)pow(10.0, mImpulseAmplitude_dB / 20) * 20;
    mImpulsePeriod = (float)(mSampleRate_Hz / mImpulseFrequency_Hz);
	mCurImpulseCounter = 0;

	mNoiseAmplitude = (float)pow(10.0, mNoiseAmplitude_dB / 20) * 20;

	mModulationDepth = (float)(mModulationDepth_Percent / 100.0);
	mModulationOmegaDelta = (float)(2.0 * PI / mModulationFrequency_Hz);
	mModulationFloor = (float)(1.0 - mModulationDepth);
	mModulationDepth = mModulationDepth / 2; // This is so a 30% modulation goes from +15% to -15%
    mModulationOmega = 0;

	mSamplesPerFrame = OutputSize[0][0];

	if ((mProduceOutputFile) && (strlen(mOutputFileName) != 0))
	mOutputFile = fopen(mOutputFileName, "w");
	else
		mOutputFile = NULL;
    return 1;
}

int TestMod::Advance(EarlabDataStream *InputStream[EarlabMaxIOStreamCount], EarlabDataStream *OutputStream[EarlabMaxIOStreamCount])
{
    int i;
	double curval;
	float cur_random;
	FloatMatrixN *Input, *Output;

	mLogger->Log("    Advance: %s", mModuleName);

	Input = ((EarlabWaveformStream *)InputStream[0])->GetData();    // Only supporting one output at the present moment
	Output = ((EarlabWaveformStream *)OutputStream[0])->GetData();  // Only supporting one output at the present moment

	if (Output->Rank(0) != mSamplesPerFrame)
		throw EarlabException("TestMod: Output size mismatch with Start()");

    for (i = 0; i < mSamplesPerFrame; i++)
    {
		curval = 0;
		if (mProduceSineSignal)
		{
			curval += mSineAmplitude * sin(mSineOmega);
		mSineOmega += mSineOmegaDelta;
		}

		if (mProduceImpulseSignal)
		{
			if (mCurImpulseCounter >= mImpulsePeriod)
			{
				mCurImpulseCounter = 0;
				curval += mImpulseAmplitude;
			}
			mCurImpulseCounter++;
		}

		if (mProduceNoiseSignal)
		{
			cur_random = mNoiseAmplitude * (float)(((float)rand() / (float)RAND_MAX) - 0.5);
			curval += cur_random;
		}

		if (mSineModulateSignal)
		{
			curval *= ((mModulationDepth * sin(mModulationOmega)) + mModulationFloor);
			mModulationOmega += mModulationOmegaDelta;
		}

		Output->Data(i) = (float)curval;

		if (mOutputFile != NULL)
	    fprintf(mOutputFile, "%g\n", curval);
    }
    return mSamplesPerFrame;
}

int TestMod::Stop(void)
{
	cout << "    Stop: " << mModuleName << endl;
    if (mOutputFile != NULL)
	fclose(mOutputFile);
	return 1;
}

int TestMod::Unload(void)
{
	cout << "    Unload: " << mModuleName << endl;
	return 1;
}

void TestMod::SetModuleName(char *ModuleName)
{
	if (mModuleName != NULL)
		delete [] mModuleName;

	mModuleName = new char[strlen(ModuleName) + 1];
	strcpy(mModuleName, ModuleName);
}

void TestMod::SetLogger(Logger *TheLogger)
{
	if (mLogger != NULL)
		delete mLogger;
	mLogger = TheLogger;
}