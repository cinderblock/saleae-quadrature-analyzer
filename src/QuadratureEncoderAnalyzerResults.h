#ifndef QUADRATURE_ENCODER_ANALYZER_RESULTS
#define QUADRATURE_ENCODER_ANALYZER_RESULTS

#include <AnalyzerResults.h>

#include "QuadratureDecoder.h"

class QuadratureEncoderAnalyzer;
class QuadratureEncoderAnalyzerSettings;

// Frame encoding (FrameV2 carries the full data as named fields for the data table and HLAs):
//   mType   quadrature::SegmentType
//   mData1  count (for a move: where it ended), as a signed 64-bit value
//   mData2  Count: the change that ended it. Index: counts since the previous index. Move: where it started.
//   mFlags  the FRAME_* flags below
#define FRAME_POSITIVE ( 1 << 0 )    // the change that ended the frame was forward
#define FRAME_NEGATIVE ( 1 << 1 )    // ... or backward
#define FRAME_HAS_SINCE ( 1 << 2 )   // Index: mData2 is valid (there was a previous index)
#define FRAME_MOVE_INDEX ( 1 << 3 )  // Move: index pulses happened during it
#define FRAME_MOVE_ERRORS ( 1 << 4 ) // Move: skipped states happened during it
#define FRAME_MOVE_JITTER ( 1 << 5 ) // Move: it ended where it started

class QuadratureEncoderAnalyzerResults : public AnalyzerResults
{
  public:
    QuadratureEncoderAnalyzerResults( QuadratureEncoderAnalyzer* analyzer, QuadratureEncoderAnalyzerSettings* settings );
    virtual ~QuadratureEncoderAnalyzerResults();

    virtual void GenerateBubbleText( U64 frame_index, Channel& channel, DisplayBase display_base );
    virtual void GenerateExportFile( const char* file, DisplayBase display_base, U32 export_type_user_id );

    virtual void GenerateFrameTabularText( U64 frame_index, DisplayBase display_base );
    virtual void GeneratePacketTabularText( U64 packet_id, DisplayBase display_base );
    virtual void GenerateTransactionTabularText( U64 transaction_id, DisplayBase display_base );

    static Frame EncodeFrame( const quadrature::Segment& segment );

    // A count in the configured units, without trailing zeros, with the unit suffix when `with_unit` is set.
    void FormatPosition( double counts, bool with_unit, char* out, size_t size ) const;

  protected:
    double FrameSeconds( const Frame& frame ) const;

    QuadratureEncoderAnalyzerSettings* mSettings;
    QuadratureEncoderAnalyzer* mAnalyzer;
};

#endif // QUADRATURE_ENCODER_ANALYZER_RESULTS
