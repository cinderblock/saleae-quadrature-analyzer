#ifndef QUADRATURE_ENCODER_ANALYZER_H
#define QUADRATURE_ENCODER_ANALYZER_H

#include <Analyzer.h>

#include "QuadratureEncoderAnalyzerSettings.h"
#include "QuadratureEncoderAnalyzerResults.h"
#include "QuadratureEncoderSimulationDataGenerator.h"
#include "QuadratureDecoder.h"

#include <memory>

class ANALYZER_EXPORT QuadratureEncoderAnalyzer : public Analyzer2
{
  public:
    QuadratureEncoderAnalyzer();
    virtual ~QuadratureEncoderAnalyzer();

    virtual void SetupResults();
    virtual void WorkerThread();

    virtual U32 GenerateSimulationData( U64 newest_sample_requested, U32 sample_rate, SimulationChannelDescriptor** simulation_channels );
    virtual U32 GetMinimumSampleRateHz();

    virtual const char* GetAnalyzerName() const;
    virtual bool NeedsRerun();

  protected:
    void EmitSegment( const quadrature::Segment& segment );

    QuadratureEncoderAnalyzerSettings mSettings;
    std::unique_ptr<QuadratureEncoderAnalyzerResults> mResults;

    QuadratureEncoderSimulationDataGenerator mSimulationDataGenerator;
    bool mSimulationInitialized;

    U32 mSampleRateHz;
};

extern "C" ANALYZER_EXPORT const char* __cdecl GetAnalyzerName();
extern "C" ANALYZER_EXPORT Analyzer* __cdecl CreateAnalyzer();
extern "C" ANALYZER_EXPORT void __cdecl DestroyAnalyzer( Analyzer* analyzer );

#endif // QUADRATURE_ENCODER_ANALYZER_H
