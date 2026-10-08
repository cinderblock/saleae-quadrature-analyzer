#include "MoveGrouper.h"

#include <algorithm>

namespace quadrature
{
    MoveGrouper::MoveGrouper( uint64_t hold_samples ) : mHoldSamples( hold_samples )
    {
    }

    void MoveGrouper::Add( const Segment& segment, std::vector<Segment>& out )
    {
        if( segment.type == SegmentType::Index || segment.type == SegmentType::Error )
        {
            if( !mInMove )
            {
                out.push_back( segment );
                return;
            }
            mMove.end = segment.end;
            if( segment.type == SegmentType::Index )
                ++mMove.index_pulses;
            else
                ++mMove.errors;
            mLastCount = segment.count; // an index may have reset it
            return;
        }

        // A continuation (the decoder was flushed mid-span) carries on the hold before it.
        if( segment.end - segment.start >= mHoldSamples || ( segment.continuation && !mInMove ) )
        {
            // A hold. It ends any move in progress, which arrived at this count.
            if( mInMove )
                EndMove( segment.count, segment.travel, out );
            out.push_back( segment );
            mHaveHold = true;
            mHoldCount = segment.count;
            mHoldTravel = segment.travel;
            return;
        }

        if( !mInMove )
        {
            mInMove = true;
            mSpans = 0;
            mFirst = segment;
            mMove = Segment();
            mMove.type = SegmentType::Move;
            mMove.start = segment.start;
            mMove.from_count = mHaveHold ? mHoldCount : segment.count;
            mFromTravel = mHaveHold ? mHoldTravel : segment.travel;
            mMove.min_count = mMove.max_count = mMove.from_count;
        }

        ++mSpans;
        mMove.end = segment.end;
        mMove.min_count = std::min( mMove.min_count, segment.count );
        mMove.max_count = std::max( mMove.max_count, segment.count );
        mLastCount = segment.count;
        mLastTravel = segment.travel;
    }

    void MoveGrouper::Tick( uint64_t now, uint64_t open_start, int64_t open_count, int64_t open_travel, std::vector<Segment>& out )
    {
        if( mInMove && now >= open_start && now - open_start >= mHoldSamples )
            EndMove( open_count, open_travel, out );
    }

    void MoveGrouper::Flush( std::vector<Segment>& out )
    {
        if( mInMove )
            EndMove( mLastCount, mLastTravel, out );
    }

    void MoveGrouper::EndMove( int64_t to_count, int64_t to_travel, std::vector<Segment>& out )
    {
        mInMove = false;
        mHaveHold = true;
        mHoldCount = to_count;
        mHoldTravel = to_travel;

        // A single short span that went nowhere is just a short count span, not a move.
        if( mSpans == 1 && to_travel == mFromTravel && mMove.index_pulses == 0 && mMove.errors == 0 )
        {
            out.push_back( mFirst );
            return;
        }

        Segment move = mMove;
        move.count = to_count;
        move.travel = to_travel;
        move.delta = to_travel - mFromTravel;
        move.direction = move.delta > 0 ? 1 : ( move.delta < 0 ? -1 : 0 );
        move.min_count = std::min( move.min_count, to_count );
        move.max_count = std::max( move.max_count, to_count );
        out.push_back( move );
    }
}
