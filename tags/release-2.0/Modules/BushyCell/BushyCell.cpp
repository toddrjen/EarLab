#include <iostream>
#include <stdio.h>
#include <math.h>
#include <memory.h>
#include <string.h>
#include <time.h>
#include "Earlab.h"
#include "BushyCell.h"
#include "CParameterFile.h"
#include "ParameterStatus.h"
#include "EarlabException.h"
#include <math.h>
#ifndef NOTWINDOWS
# include <windows.h>
#else
# include <unistd.h>
# define LARGE_INTEGER unsigned int
#endif

using namespace std;

BushyCell::BushyCell()
{
    memset(this, 0, sizeof(BushyCell));
	mLogger = new Logger();
	SetModuleName("BushyCell");
}

BushyCell::~BushyCell()
{
	if (mModuleName != NULL)
		delete [] mModuleName;
}

int BushyCell::ReadParameters(char *ParameterFileName)
{
	if (mModuleName == NULL)
	    return ReadParameters(ParameterFileName, "BushyCell");
	else
	    return ReadParameters(ParameterFileName, mModuleName);
}

int BushyCell::ReadParameters(char *ParameterFileName, char *SectionName)
{
    CParameterFile theParamFile(ParameterFileName);
    ParameterStatus Status;

	mLogger->Log("    ReadParameters: %s \"%s\"", mModuleName, ParameterFileName);

    // Number of channels and Frame Size are passed as parameters to Start, see that function for details
    Status = theParamFile.GetParameter(SectionName, "SampleRate_Hz", mSampleRate_Hz, 0.0);
    Status = theParamFile.GetParameter(SectionName, "FrameLength_Seconds", mFrameLength_Seconds, 0.0);
    Status = theParamFile.GetParameter(SectionName, "SynapticStrength", mSynapticStrength, 0.0);
    Status = theParamFile.GetParameter(SectionName, "SynapticTau_Seconds", mSynapticTau, 0.0);
    Status = theParamFile.GetParameter(SectionName, "CellTemperature_DegreesC", mCellTemperature_DegreesC, 0.0);
	Status = theParamFile.GetParameter(SectionName, "MaxSynapseCount", mMaxSynapseCount, 0);
    return 1;
}

int BushyCell::Start(int NumInputs,  EarlabDataStreamType InputTypes[EarlabMaxIOStreamCount],  int InputSize[EarlabMaxIOStreamCount][EarlabMaxIOStreamDimensions],
			int NumOutputs, EarlabDataStreamType OutputTypes[EarlabMaxIOStreamCount], int OutputSize[EarlabMaxIOStreamCount][EarlabMaxIOStreamDimensions],
			unsigned long OutputElementCounts[EarlabMaxIOStreamCount])
{
	int CellCount;
	int Channel;

	cout << "    Start: " << mModuleName << endl;

    // Perform some validation on my parameters to make sure I can handle the requested input and output streams...
    if (NumInputs != 1)
	throw EarlabException("%s: Currently this module can only handle one input stream.  Sorry!", mModuleName);

    if (NumOutputs != 1)
	throw EarlabException("%s: Currently this module can only handle one output stream.  Sorry!", mModuleName);

	if (InputTypes[0] != SpikeData)
		throw EarlabException("%s: Currently this module can only handle spike input data streams.  Sorry!", mModuleName);

	if (OutputTypes[0] != SpikeData)
		throw EarlabException("%s: Currently this module can only handle spike output data streams.  Sorry!", mModuleName);

    if (InputSize[0][0] != OutputSize[0][0])
	throw EarlabException("%s: Input and output channel counts must be identical.  Sorry!", mModuleName);

    if (InputSize[0][2] != 0)
	throw EarlabException("%s: Input data must be two-dimensional array.  Sorry!", mModuleName);

    if (OutputSize[0][1] == 0)
	throw EarlabException("%s: Second dimension of output signal must be non-zero cells per channel.  Sorry!", mModuleName);

    if (OutputSize[0][2] != 0)
	throw EarlabException("%s: Output data must be two-dimensional array.  Sorry!", mModuleName);

	CellCount = OutputSize[0][0] * OutputSize[0][1];

	NumChannels = InputSize[0][0];
	NumInputReps = InputSize[0][1];
	NumOutputReps = OutputSize[0][1];

	if (InputSize[0][1] < mMaxSynapseCount)
	{
		//mMaxSynapseCount = InputSize[0][1];
		throw EarlabException("%s: Warning: Max synapse count %d specified in parameter file, but only %d input cells are available", mMaxSynapseCount, InputSize[0][1]);
	}
	else
	{
		int OutputRep, Channel, CurInputCell, CurSynapse, CurInputCellID;;
		LARGE_INTEGER seed;
		double RandomNumber;
		int RandomIndex;
		bool Duplicate;

#ifndef _WIN32
		seed = time(0)*getpid();
		srand(seed);
#else
		QueryPerformanceCounter(&seed);
		srand(seed.LowPart);
#endif
		SynapseList = new int **[NumOutputReps];
		for (OutputRep = 0; OutputRep < NumOutputReps; OutputRep++)
		{
			SynapseList[OutputRep] = new int *[NumChannels];
			for (Channel = 0; Channel < NumChannels; Channel++)
			{
				SynapseList[OutputRep][Channel] = new int [mMaxSynapseCount];
				for (CurSynapse = 0; CurSynapse < mMaxSynapseCount; CurSynapse++)
					SynapseList[OutputRep][Channel][CurSynapse] = -1;
				CurSynapse = 0;
				// Make sure we fill the required number with non-repeating input cell IDs
				while (CurSynapse < mMaxSynapseCount)
				{
					// Generate a random number between zero and the number of potential input cells
					RandomNumber = (((double)rand() / (double)RAND_MAX) * (double)NumInputReps);
					// Make that floating point number into an integer, which we will use as the index of the potential input cell to the current bushy cell
					RandomIndex = (int)RandomNumber;
					CurInputCellID = (Channel * NumInputReps) + RandomIndex;

					// Make sure this input cell index has not already been used for the current bushy cell
					Duplicate = false;
					for (CurInputCell = 0; CurInputCell < CurSynapse; CurInputCell++)
					{
						if (CurInputCellID == SynapseList[OutputRep][Channel][CurInputCell])
						{
							Duplicate = true;
							break;
						}
					}
					// If the index has not already been used, use it, increment the count of input cells found, and pick another one until we have enough
					if (!Duplicate)
					{
						SynapseList[OutputRep][Channel][CurSynapse] = CurInputCellID;
						CurSynapse++;
					}
				}
			}
		}
	}

	OutputElementCounts[0] = (int)(CellCount * (mFrameLength_Seconds * 1000));

	BushyCells = new HHCompartment **[NumChannels];
	Synapses = new HHSynapse **[NumChannels];
	WasUnderThreshold = new bool *[NumChannels];

	for (Channel = 0; Channel < NumChannels; Channel++)
	{
		BushyCells[Channel] = new HHCompartment *[NumOutputReps];
		Synapses[Channel] = new HHSynapse *[NumOutputReps];
		WasUnderThreshold[Channel] = new bool [NumOutputReps];
		for (int OutputRep = 0; OutputRep < NumOutputReps; OutputRep++)
		{
			Synapses[Channel][OutputRep] = new HHSynapse(mSampleRate_Hz, mSynapticTau, mSynapticStrength);
			BushyCells[Channel][OutputRep] = new HHCompartment(mSampleRate_Hz, -60.0e-3, mCellTemperature_DegreesC);
			WasUnderThreshold[Channel][OutputRep] = true;
		}
	}

	SamplesPerFrame = (int)(mFrameLength_Seconds * mSampleRate_Hz);

	return 1;
}

int BushyCell::Advance(EarlabDataStream *InputStream[EarlabMaxIOStreamCount], EarlabDataStream *OutputStream[EarlabMaxIOStreamCount])
{
	int counter = 0;
	SpikeStream *Input;
	SpikeStream *Output;
	int Sample, Channel, Rep;
	double MembraneVoltage_Volts;
	int CellID;

	mLogger->Log("    Advance: %s", mModuleName);

	Input =  ((EarlabSpikeStream *)InputStream[0])->GetData();
	Output = ((EarlabSpikeStream *)OutputStream[0])->GetData();

	Input->SetSampleRate_Hz(mSampleRate_Hz);
	Output->SetSampleRate_Hz(mSampleRate_Hz);
	Input->NewFrame();
	Output->NewFrame();
	for (Sample = 0; Sample < SamplesPerFrame; Sample++)
	{
		for (Channel = 0; Channel < NumChannels; Channel++)
		{
			for (Rep = 0; Rep < NumOutputReps; Rep++)
			{
				CellID = (Channel * NumOutputReps) + Rep;
				Synapses[Channel][Rep]->Fire(Input->CountSpikes(SynapseList[Rep][Channel], mMaxSynapseCount));
				Synapses[Channel][Rep]->Step();
				BushyCells[Channel][Rep]->Step(&(Synapses[Channel][Rep]), 1, NULL, 0);
				MembraneVoltage_Volts = BushyCells[Channel][Rep]->GetVoltage_V();
				if (MembraneVoltage_Volts >= -25e-3)
				{
					if (WasUnderThreshold[Channel][Rep])
						Output->Fire(CellID);
					WasUnderThreshold[Channel][Rep] = false;
				}
				else
					WasUnderThreshold[Channel][Rep] = true;
			} // for (Channel)
		}
		Input->Step();
		Output->Step();
	}

	return counter + 1;
}

int BushyCell::Stop(void)
{
	cout << "    Stop: " << mModuleName << endl;

    return 1;
}

int BushyCell::Unload(void)
{
	cout << "    Unload: " << mModuleName << endl;

    return 1;
}

void BushyCell::SetModuleName(char *ModuleName)
{
	if (mModuleName != NULL)
		delete [] mModuleName;

	mModuleName = new char[strlen(ModuleName) + 1];
	strcpy(mModuleName, ModuleName);
}

void BushyCell::SetLogger(Logger *TheLogger)
{
	if (mLogger != NULL)
		delete mLogger;
	mLogger = TheLogger;
}