#ifndef QUADRATURE_ENCODER_ANALYZER_SETTINGS
#define QUADRATURE_ENCODER_ANALYZER_SETTINGS

#include <AnalyzerSettings.h>
#include <AnalyzerTypes.h>

#include "QuadratureDecoder.h"

#include <memory>

enum class ResultMode : U32
{
    EveryCount = 0,    // one result per count
    MovesAndHolds = 1, // runs of quick counts grouped into one move
};

enum class PositionUnits : U32
{
    Counts = 0,
    Degrees = 1,
    Revolutions = 2,
};

class QuadratureEncoderAnalyzerSettings : public AnalyzerSettings
{
  public:
    QuadratureEncoderAnalyzerSettings();
    virtual ~QuadratureEncoderAnalyzerSettings();

    virtual bool SetSettingsFromInterfaces();
    void UpdateInterfacesFromSettings();
    virtual void LoadSettings( const char* settings );
    virtual const char* SaveSettings();

    quadrature::DecoderConfig MakeDecoderConfig() const;
    bool HasIndex() const;

    // Counts per revolution after decoding: lines per revolution times the decoding factor.
    double CountsPerRevolution() const;
    // Converts counts to the configured display unit.
    double ToUnits( double counts ) const;
    const char* UnitSuffix() const;

    Channel mA;
    Channel mB;
    Channel mZ;    // UNDEFINED_CHANNEL when there is no index
    U32 mDecoding; // 4, 2 or 1
    U32 mIndexMode;
    U32 mResultMode;
    U32 mHoldTime; // milliseconds; with MovesAndHolds, shorter counts are part of a move
    U32 mUnits;
    U32 mLinesPerRevolution;
    bool mInvert;

  protected:
    void UpdateChannels( bool is_used );

    std::unique_ptr<AnalyzerSettingInterfaceChannel> mAInterface;
    std::unique_ptr<AnalyzerSettingInterfaceChannel> mBInterface;
    std::unique_ptr<AnalyzerSettingInterfaceChannel> mZInterface;
    std::unique_ptr<AnalyzerSettingInterfaceNumberList> mDecodingInterface;
    std::unique_ptr<AnalyzerSettingInterfaceNumberList> mIndexModeInterface;
    std::unique_ptr<AnalyzerSettingInterfaceNumberList> mResultModeInterface;
    std::unique_ptr<AnalyzerSettingInterfaceInteger> mHoldTimeInterface;
    std::unique_ptr<AnalyzerSettingInterfaceNumberList> mUnitsInterface;
    std::unique_ptr<AnalyzerSettingInterfaceInteger> mLinesPerRevolutionInterface;
    std::unique_ptr<AnalyzerSettingInterfaceBool> mInvertInterface;
};

#endif // QUADRATURE_ENCODER_ANALYZER_SETTINGS
