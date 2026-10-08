#ifndef QUADRATURE_ENCODER_ANALYZER_H
#define QUADRATURE_ENCODER_ANALYZER_H

#include <Analyzer.h>

#include "QuadratureEncoderAnalyzerSettings.h"
#include "QuadratureEncoderAnalyzerResults.h"
#include "QuadratureEncoderSimulationDataGenerator.h"
#include "IdleFlusher.h"
#include "MoveGrouper.h"
#include "QuadratureDecoder.h"

#include <memory>
#include <vector>

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
    void EmitResults( bool flush );
    void FlushOpen();

    QuadratureEncoderAnalyzerSettings mSettings;
    std::unique_ptr<QuadratureEncoderAnalyzerResults> mResults;

    QuadratureEncoderSimulationDataGenerator mSimulationDataGenerator;
    bool mSimulationInitialized;

    U32 mSampleRateHz;

    // Decoding state. Members rather than worker locals because the flusher thread uses them too,
    // always under mFlusher.Lock().
    IdleFlusher mFlusher;
    std::unique_ptr<quadrature::QuadratureDecoder> mDecoder;
    std::unique_ptr<quadrature::MoveGrouper> mGrouper;
    std::vector<quadrature::Segment> mSegments;
    std::vector<quadrature::Segment> mGrouped;
    bool mUseZ;
    U64 mFilled;
};

extern "C" ANALYZER_EXPORT const char* __cdecl GetAnalyzerName();
extern "C" ANALYZER_EXPORT Analyzer* __cdecl CreateAnalyzer();
extern "C" ANALYZER_EXPORT void __cdecl DestroyAnalyzer( Analyzer* analyzer );

#endif // QUADRATURE_ENCODER_ANALYZER_H
