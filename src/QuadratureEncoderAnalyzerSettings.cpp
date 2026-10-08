#include "QuadratureEncoderAnalyzerSettings.h"

#include <AnalyzerHelpers.h>

#include <vector>

QuadratureEncoderAnalyzerSettings::QuadratureEncoderAnalyzerSettings()
    : mA( UNDEFINED_CHANNEL ),
      mB( UNDEFINED_CHANNEL ),
      mZ( UNDEFINED_CHANNEL ),
      mDecoding( 4 ),
      mIndexMode( U32( quadrature::IndexMode::Mark ) ),
      mResultMode( U32( ResultMode::EveryCount ) ),
      mHoldTime( 20 ),
      mUnits( U32( PositionUnits::Counts ) ),
      mLinesPerRevolution( 1024 ),
      mInvert( false )
{
    mAInterface.reset( new AnalyzerSettingInterfaceChannel() );
    mAInterface->SetTitleAndTooltip( "A", "Encoder channel A. Forward is A leading B." );
    AddInterface( mAInterface.get() );

    mBInterface.reset( new AnalyzerSettingInterfaceChannel() );
    mBInterface->SetTitleAndTooltip( "B", "Encoder channel B" );
    AddInterface( mBInterface.get() );

    mZInterface.reset( new AnalyzerSettingInterfaceChannel() );
    mZInterface->SetTitleAndTooltip( "Z (index)", "Optional index pulse, once per revolution" );
    mZInterface->SetSelectionOfNoneIsAllowed( true );
    AddInterface( mZInterface.get() );

    mDecodingInterface.reset( new AnalyzerSettingInterfaceNumberList() );
    mDecodingInterface->SetTitleAndTooltip( "Decoding", "Counts per encoder cycle (line)" );
    mDecodingInterface->AddNumber( 4, "X4: every A and B edge", "Four counts per line" );
    mDecodingInterface->AddNumber( 2, "X2: every A edge", "Two counts per line" );
    mDecodingInterface->AddNumber( 1, "X1: one A edge per cycle", "One count per line" );
    AddInterface( mDecodingInterface.get() );

    mIndexModeInterface.reset( new AnalyzerSettingInterfaceNumberList() );
    mIndexModeInterface->SetTitleAndTooltip( "Index", "What a rising edge on Z does (when Z is selected)" );
    mIndexModeInterface->AddNumber( double( quadrature::IndexMode::Mark ), "Mark index pulses",
                                    "Show each index pulse and the counts travelled since the previous one" );
    mIndexModeInterface->AddNumber( double( quadrature::IndexMode::Reset ), "Reset count to 0 at index",
                                    "As Mark, and the count restarts at 0 on each index pulse" );
    mIndexModeInterface->AddNumber( double( quadrature::IndexMode::Ignore ), "Ignore", "Z is not used" );
    AddInterface( mIndexModeInterface.get() );

    mResultModeInterface.reset( new AnalyzerSettingInterfaceNumberList() );
    mResultModeInterface->SetTitleAndTooltip( "Results", "One result per count, or whole moves between holds (reads better zoomed out)" );
    mResultModeInterface->AddNumber( double( ResultMode::EveryCount ), "Every count", "A result each time the count changes" );
    mResultModeInterface->AddNumber( double( ResultMode::MovesAndHolds ), "Moves and holds",
                                     "Quick successive counts become one move result, from start to end" );
    AddInterface( mResultModeInterface.get() );

    mHoldTimeInterface.reset( new AnalyzerSettingInterfaceInteger() );
    mHoldTimeInterface->SetTitleAndTooltip( "Hold time (ms)",
                                            "With moves and holds: a count held at least this long ends a move and is shown as a hold" );
    mHoldTimeInterface->SetMin( 1 );
    mHoldTimeInterface->SetMax( 1000000 );
    AddInterface( mHoldTimeInterface.get() );

    mUnitsInterface.reset( new AnalyzerSettingInterfaceNumberList() );
    mUnitsInterface->SetTitleAndTooltip( "Display units", "Units for the position in results" );
    mUnitsInterface->AddNumber( double( PositionUnits::Counts ), "Counts", "" );
    mUnitsInterface->AddNumber( double( PositionUnits::Degrees ), "Degrees", "Uses lines per revolution" );
    mUnitsInterface->AddNumber( double( PositionUnits::Revolutions ), "Revolutions", "Uses lines per revolution" );
    AddInterface( mUnitsInterface.get() );

    mLinesPerRevolutionInterface.reset( new AnalyzerSettingInterfaceInteger() );
    mLinesPerRevolutionInterface->SetTitleAndTooltip( "Lines per revolution",
                                                      "Encoder resolution in lines (PPR); X4 decoding gives four counts per line" );
    mLinesPerRevolutionInterface->SetMin( 1 );
    mLinesPerRevolutionInterface->SetMax( 100000000 );
    AddInterface( mLinesPerRevolutionInterface.get() );

    mInvertInterface.reset( new AnalyzerSettingInterfaceBool() );
    mInvertInterface->SetTitleAndTooltip( "Invert direction", "Count B leading A as forward" );
    mInvertInterface->SetCheckBoxText( "Invert direction" );
    AddInterface( mInvertInterface.get() );

    AddExportOption( 0, "Export as text/csv file" );
    AddExportExtension( 0, "text", "txt" );
    AddExportExtension( 0, "csv", "csv" );

    UpdateInterfacesFromSettings();
    UpdateChannels( false );
}

QuadratureEncoderAnalyzerSettings::~QuadratureEncoderAnalyzerSettings()
{
}

void QuadratureEncoderAnalyzerSettings::UpdateChannels( bool is_used )
{
    ClearChannels();
    AddChannel( mA, "A", is_used );
    AddChannel( mB, "B", is_used );
    AddChannel( mZ, "Z (index)", is_used && mZ != UNDEFINED_CHANNEL );
}

bool QuadratureEncoderAnalyzerSettings::SetSettingsFromInterfaces()
{
    const Channel a = mAInterface->GetChannel();
    const Channel b = mBInterface->GetChannel();
    const Channel z = mZInterface->GetChannel();
    if( a == UNDEFINED_CHANNEL || b == UNDEFINED_CHANNEL )
    {
        SetErrorText( "Please select channels A and B." );
        return false;
    }

    std::vector<Channel> channels = { a, b };
    if( z != UNDEFINED_CHANNEL )
        channels.push_back( z );
    if( AnalyzerHelpers::DoChannelsOverlap( channels.data(), U32( channels.size() ) ) )
    {
        SetErrorText( "A, B and Z each need their own channel." );
        return false;
    }

    mA = a;
    mB = b;
    mZ = z;
    mDecoding = U32( mDecodingInterface->GetNumber() );
    mIndexMode = U32( mIndexModeInterface->GetNumber() );
    mResultMode = U32( mResultModeInterface->GetNumber() );
    mHoldTime = U32( mHoldTimeInterface->GetInteger() );
    mUnits = U32( mUnitsInterface->GetNumber() );
    mLinesPerRevolution = U32( mLinesPerRevolutionInterface->GetInteger() );
    mInvert = mInvertInterface->GetValue();

    UpdateChannels( true );
    return true;
}

void QuadratureEncoderAnalyzerSettings::UpdateInterfacesFromSettings()
{
    mAInterface->SetChannel( mA );
    mBInterface->SetChannel( mB );
    mZInterface->SetChannel( mZ );
    mDecodingInterface->SetNumber( mDecoding );
    mIndexModeInterface->SetNumber( mIndexMode );
    mResultModeInterface->SetNumber( mResultMode );
    mHoldTimeInterface->SetInteger( mHoldTime );
    mUnitsInterface->SetNumber( mUnits );
    mLinesPerRevolutionInterface->SetInteger( mLinesPerRevolution );
    mInvertInterface->SetValue( mInvert );
}

void QuadratureEncoderAnalyzerSettings::LoadSettings( const char* settings )
{
    SimpleArchive text_archive;
    text_archive.SetString( settings );

    text_archive >> mA;
    text_archive >> mB;
    text_archive >> mZ;
    text_archive >> mDecoding;
    text_archive >> mIndexMode;
    text_archive >> mResultMode;
    text_archive >> mHoldTime;
    text_archive >> mUnits;
    text_archive >> mLinesPerRevolution;
    text_archive >> mInvert;

    UpdateChannels( true );
    UpdateInterfacesFromSettings();
}

const char* QuadratureEncoderAnalyzerSettings::SaveSettings()
{
    SimpleArchive text_archive;

    text_archive << mA;
    text_archive << mB;
    text_archive << mZ;
    text_archive << mDecoding;
    text_archive << mIndexMode;
    text_archive << mResultMode;
    text_archive << mHoldTime;
    text_archive << mUnits;
    text_archive << mLinesPerRevolution;
    text_archive << mInvert;

    return SetReturnString( text_archive.GetString() );
}

bool QuadratureEncoderAnalyzerSettings::HasIndex() const
{
    return mZ != UNDEFINED_CHANNEL && quadrature::IndexMode( mIndexMode ) != quadrature::IndexMode::Ignore;
}

quadrature::DecoderConfig QuadratureEncoderAnalyzerSettings::MakeDecoderConfig() const
{
    quadrature::DecoderConfig config;
    config.decoding = quadrature::Decoding( mDecoding );
    config.index_mode = HasIndex() ? quadrature::IndexMode( mIndexMode ) : quadrature::IndexMode::Ignore;
    config.invert = mInvert;
    return config;
}

double QuadratureEncoderAnalyzerSettings::CountsPerRevolution() const
{
    return double( mLinesPerRevolution ) * double( mDecoding );
}

double QuadratureEncoderAnalyzerSettings::ToUnits( double counts ) const
{
    switch( PositionUnits( mUnits ) )
    {
    case PositionUnits::Degrees:
        return counts * 360.0 / CountsPerRevolution();
    case PositionUnits::Revolutions:
        return counts / CountsPerRevolution();
    case PositionUnits::Counts:
    default:
        return counts;
    }
}

const char* QuadratureEncoderAnalyzerSettings::UnitSuffix() const
{
    switch( PositionUnits( mUnits ) )
    {
    case PositionUnits::Degrees:
        return "\xC2\xB0"; // degree sign, UTF-8
    case PositionUnits::Revolutions:
        return " rev";
    case PositionUnits::Counts:
    default:
        return "";
    }
}
