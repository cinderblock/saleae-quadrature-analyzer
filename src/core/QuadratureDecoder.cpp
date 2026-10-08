#include "QuadratureDecoder.h"

#include <algorithm>

namespace quadrature
{
    QuadratureDecoder::QuadratureDecoder( const DecoderConfig& config ) : mConfig( config )
    {
    }

    int QuadratureDecoder::State( bool a, bool b )
    {
        static const int kState[ 2 ][ 2 ] = { { 0, 3 }, { 1, 2 } }; // [a][b]
        return kState[ a ][ b ];
    }

    void QuadratureDecoder::Start( uint64_t sample, bool a, bool b, bool z )
    {
        mA = a;
        mB = b;
        mZ = z;
        mCount = 0;
        mTravel = 0;
        mOpenStart = sample;
        mHaveIndex = false;
        mIndexTravel = 0;
    }

    void QuadratureDecoder::Update( uint64_t sample, bool a, bool b, bool z, std::vector<Segment>& out )
    {
        // An earlier one-sample result may already occupy this sample.
        sample = std::max( sample, mOpenStart );

        const int from = State( mA, mB );
        const int to = State( a, b );
        const bool a_changed = a != mA;
        const bool b_changed = b != mB;
        mA = a;
        mB = b;

        if( a_changed && b_changed )
        {
            Segment error;
            EmitInstant( SegmentType::Error, sample, out, error );
            sample = mOpenStart;
        }
        else if( from != to )
        {
            int step = ( ( to - from ) & 3 ) == 1 ? 1 : -1;
            if( mConfig.invert )
                step = -step;

            bool counts = true;
            if( mConfig.decoding == Decoding::X2 )
                counts = a_changed;
            else if( mConfig.decoding == Decoding::X1 )
                counts = a_changed && !b;

            if( counts )
            {
                CloseOpen( sample, step, out );
                mCount += step;
                mTravel += step;
                mOpenStart = sample;
            }
        }

        const bool index_rise = z && !mZ;
        mZ = z;
        if( index_rise && mConfig.index_mode != IndexMode::Ignore )
        {
            Segment index;
            index.has_since_index = mHaveIndex;
            index.since_index = mTravel - mIndexTravel;
            mHaveIndex = true;
            mIndexTravel = mTravel;
            if( mConfig.index_mode == IndexMode::Reset )
                mCount = 0;
            EmitInstant( SegmentType::Index, sample, out, index );
        }
    }

    void QuadratureDecoder::Flush( uint64_t end, std::vector<Segment>& out )
    {
        CloseOpen( std::max( end, mOpenStart ), 0, out );
        mOpenStart = std::max( end, mOpenStart );
    }

    void QuadratureDecoder::CloseOpen( uint64_t end, int64_t delta, std::vector<Segment>& out )
    {
        if( end <= mOpenStart )
            return;

        Segment segment;
        segment.type = SegmentType::Count;
        segment.start = mOpenStart;
        segment.end = end;
        segment.count = mCount;
        segment.travel = mTravel;
        segment.delta = delta;
        segment.direction = delta > 0 ? 1 : ( delta < 0 ? -1 : 0 );
        out.push_back( segment );
    }

    void QuadratureDecoder::EmitInstant( SegmentType type, uint64_t sample, std::vector<Segment>& out, Segment instant )
    {
        // The count span so far ends here; the instant takes one sample; counting resumes after it.
        CloseOpen( sample, 0, out );
        instant.type = type;
        instant.start = sample;
        instant.end = sample + 1;
        instant.count = mCount;
        instant.travel = mTravel;
        out.push_back( instant );
        mOpenStart = sample + 1;
    }
}
