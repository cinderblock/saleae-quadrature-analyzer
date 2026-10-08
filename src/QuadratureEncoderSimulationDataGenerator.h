#ifndef QUADRATURE_ENCODER_SIMULATION_DATA_GENERATOR
#define QUADRATURE_ENCODER_SIMULATION_DATA_GENERATOR

#include <SimulationChannelDescriptor.h>

#include "EncoderWaveform.h"

#include <memory>
#include <vector>

class QuadratureEncoderAnalyzerSettings;

// Plays EncoderWaveform's demo program on the A, B (and Z, if selected) channels of Logic's simulation device.
class QuadratureEncoderSimulationDataGenerator
{
  public:
    QuadratureEncoderSimulationDataGenerator();
    ~QuadratureEncoderSimulationDataGenerator();

    void Initialize( U32 simulation_sample_rate, QuadratureEncoderAnalyzerSettings* settings );
    U32 GenerateSimulationData( U64 newest_sample_requested, U32 sample_rate, SimulationChannelDescriptor** simulation_channels );

  protected:
    QuadratureEncoderAnalyzerSettings* mSettings;
    U32 mSimulationSampleRateHz;

    std::unique_ptr<quadrature::EncoderWaveform> mWaveform;
    std::vector<quadrature::Transition> mTransitions;
    U64 mGenerated;

    SimulationChannelDescriptorGroup mGroup;
    SimulationChannelDescriptor* mChannels[ quadrature::CHANNEL_COUNT ]; // nullptr for Z when it is not selected
};

#endif // QUADRATURE_ENCODER_SIMULATION_DATA_GENERATOR
