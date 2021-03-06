/**@file
	@brief Radio Resource messages, GSM 04.08 9.1.
*/

/*
* Copyright 2008, 2009 Free Software Foundation, Inc.
* Copyright 2010 Kestrel Signal Processing, Inc.
*
* This software is distributed under the terms of the GNU Affero Public License.
* See the COPYING file in the main directory for details.
*
* This use of this software may be subject to additional restrictions.
* See the LEGAL file in the main directory for details.

	This program is free software: you can redistribute it and/or modify
	it under the terms of the GNU Affero General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU Affero General Public License for more details.

	You should have received a copy of the GNU Affero General Public License
	along with this program.  If not, see <http://www.gnu.org/licenses/>.

*/

#include <iterator> // for L3APDUData::text

#include "GSML3RRElements.h"

#include <Logger.h>



using namespace std;
using namespace GSM;




void L3CellOptionsBCCH::writeV(L3Frame& dest, size_t &wp) const
{
	dest.writeField(wp,0,1);
	dest.writeField(wp,mPWRC,1);
	dest.writeField(wp,mDTX,2);
	dest.writeField(wp,mRADIO_LINK_TIMEOUT,4);
}



void L3CellOptionsBCCH::text(ostream& os) const
{
	os << "PWRC=" << mPWRC;
	os << " DTX=" << mDTX;
	os << " RADIO_LINK_TIMEOUT=" << mRADIO_LINK_TIMEOUT;
}



void L3CellOptionsSACCH::writeV(L3Frame& dest, size_t &wp) const
{
	dest.writeField(wp,(mDTX>>2)&0x01,1);
	dest.writeField(wp,mPWRC,1);
	dest.writeField(wp,mDTX&0x03,2);
	dest.writeField(wp,mRADIO_LINK_TIMEOUT,4);
}



void L3CellOptionsSACCH::text(ostream& os) const
{
	os << "PWRC=" << mPWRC;
	os << " DTX=" << mDTX;
	os << " RADIO_LINK_TIMEOUT=" << mRADIO_LINK_TIMEOUT;
}



void L3CellSelectionParameters::writeV(L3Frame& dest, size_t &wp) const
{
	dest.writeField(wp,mCELL_RESELECT_HYSTERESIS,3);
	dest.writeField(wp,mMS_TXPWR_MAX_CCH,5);
	dest.writeField(wp,mACS,1);
	dest.writeField(wp,mNECI,1);
	dest.writeField(wp,mRXLEV_ACCESS_MIN,6);
}




void L3CellSelectionParameters::text(ostream& os) const
{
	os << "CELL-RESELECT-HYSTERESIS=" << mCELL_RESELECT_HYSTERESIS;
	os << " MS-TXPWR-MAX-CCH=" << mMS_TXPWR_MAX_CCH;
	os << " ACS=" << mACS;
	os << " NECI=" << mNECI;
	os << " RXLEV-ACCESS-MIN=" << mRXLEV_ACCESS_MIN;
}







void L3ControlChannelDescription::writeV(L3Frame& dest, size_t &wp) const
{
	dest.writeField(wp,0,1);
	dest.writeField(wp,mATT,1);
	dest.writeField(wp,mBS_AG_BLKS_RES,3);
	dest.writeField(wp,mCCCH_CONF,3);
	dest.writeField(wp,0,5);
	dest.writeField(wp,mBS_PA_MFRMS,3);
	dest.writeField(wp,mT3212,8);
}



void L3ControlChannelDescription::text(ostream& os) const
{
	os << "ATT=" << mATT;
	os << " BS_AG_BLKS_RES=" << mBS_AG_BLKS_RES;
	os << " CCCH_CONF=" << mCCCH_CONF;
	os << " BS_PA_MFRMS=" << mBS_PA_MFRMS;
	os << " T3212=" << mT3212;
}


bool L3FrequencyList::contains(unsigned wARFCN) const
{
	for (unsigned i=0; i<mARFCNs.size(); i++) {
		if (mARFCNs[i]==wARFCN) return true;
	}
	return false;
}


unsigned L3FrequencyList::base() const
{
	if (mARFCNs.size()==0) return 0;
	unsigned retVal = mARFCNs[0];
	for (unsigned i=1; i<mARFCNs.size(); i++) {
		unsigned thisVal = mARFCNs[i];
		if (thisVal<retVal) retVal=thisVal;
	}
	return retVal;
}


unsigned L3FrequencyList::spread() const
{
	if (mARFCNs.size()==0) return 0;
	unsigned max = mARFCNs[0];
	for (unsigned i=0; i<mARFCNs.size(); i++) {
		if (mARFCNs[i]>max) max=mARFCNs[i];
	}
	return max - base();
}


void L3FrequencyList::writeV(L3Frame& dest, size_t &wp) const
{
	// If this were used as Frequency List, it had to be coded
	// as the variable bit map format, GSM 04.08 10.5.2.13.7.
	// But it is used as Cell Channel Description and is coded
	// as the variable bit map format, GSM 04.08 10.5.2.1b.7.
	// Difference is in abscence of Length field.
	
	// The header occupies first 7 most significant bits of
	// the first V-part octet and should be 1000111b=0x47 for
	// the variable length bitmap.
	dest.writeField(wp,0x47,7);

	// base ARFCN
	unsigned baseARFCN = base();
	dest.writeField(wp,baseARFCN,10);
	// bit map
	unsigned delta = spread();
	unsigned numBits = 8*lengthV() - 17;
	if (numBits<delta) { LOG(ALERT) << "L3FrequencyList cannot encode full ARFCN set"; }
	for (unsigned i=0; i<numBits; i++) {
		unsigned thisARFCN = baseARFCN + 1 + i;
		if (contains(thisARFCN)) dest.writeField(wp,1,1);
		else dest.writeField(wp,0,1);
	}
}



void L3FrequencyList::text(ostream& os) const
{
	int size = mARFCNs.size();
	for (int i=0; i<size; i++) {
		os << mARFCNs[i] << " ";
	}
}




void L3CellChannelDescription::writeV(L3Frame& dest, size_t& wp) const
{
	dest.fillField(wp,0,3);
	L3FrequencyList::writeV(dest,wp);
}



void L3NeighborCellsDescription::writeV(L3Frame& dest, size_t& wp) const
{
	dest.fillField(wp,0,3);
	L3FrequencyList::writeV(dest,wp);
}



void L3NeighborCellsDescription::text(ostream& os) const
{
	os << "EXT-IND=0 BA-IND=0 ";
	os << " ARFCNs=(";
	L3FrequencyList::text(os);
	os << ")";
}




void L3NCCPermitted::writeV(L3Frame& dest, size_t &wp) const
{
	dest.writeField(wp,mPermitted,8);
}



void L3NCCPermitted::text(ostream& os) const
{
	os << hex << "0x" << mPermitted << dec;
}




void L3RACHControlParameters::writeV(L3Frame& dest, size_t &wp) const
{
	// GMS 04.08 10.5.2.29
	dest.writeField(wp, mMaxRetrans, 2);
	dest.writeField(wp, mTxInteger, 4);
	dest.writeField(wp, mCellBarAccess, 1);
	dest.writeField(wp, mRE, 1);
	dest.writeField(wp, mAC, 16);
}


void L3RACHControlParameters::text(ostream& os) const
{
	os << "maxRetrans=" << mMaxRetrans;
	os << " txInteger=" << mTxInteger;
	os << " cellBarAccess=" << mCellBarAccess;
	os << " RE=" << mRE;
	os << hex << " AC=0x" << mAC << dec;
}

void L3SI3RestOctets::writeV(L3Frame& dest, size_t &wp) const
{
	// GMS 04.08 10.5.2.34
	dest.writeField(wp, 0, 1); // "L" Optional selection parameters: Not present
	dest.writeField(wp, 0, 1); // "L" Optional Power offset: Not present
	dest.writeField(wp, 1, 1); // "L" System Information 2ter Indicator: Not present
	dest.writeField(wp, 0, 1); // "L" Early Classmark Sending Control: Not present
	dest.writeField(wp, 1, 1); // "L" Scheduling if and where: Not present
	dest.writeField(wp, 1, 1); // "H" GPRS Indicator: Present
	dest.writeField(wp, mRA_COLOUR, 3);     // RA COLOUR
	dest.writeField(wp, mSI13_POSITION, 1); // SI13 POSITION
	// spare padding
	dest.writeField(wp, 0x2b, 6);
	for (int i=0; i<2; i++)
		dest.writeField(wp,0x2b,8);
}



void L3SI3RestOctets::text(ostream& os) const
{

	os << "RA_COLOUR=" << mRA_COLOUR;
	os << "SI13_POSITION=" << mSI13_POSITION;
}

void L3SI13RestOctets::writeV(L3Frame& dest, size_t &wp) const
{
	// GMS 04.08 10.5.2.37b
	dest.writeField(wp, 0x1, 1);               // "H" SI 13 Rest Octets: Present
	dest.writeField(wp, mBCCH_CHANGE_MARK, 3); // BCCH_CHANGE_MARK
	dest.writeField(wp, mSI_CHANGE_FIELD, 4);  // SI_CHANGE_FIELD
	dest.writeField(wp, 0x1, 1);               // "1" GPRS Mobile Allocation: Present
	dest.writeField(wp, mSI13_CHANGE_MARK, 2); // SI13_CHANGE_MARK

	// GPRS Mobile Allocation GSM 04.60 12.10a
	dest.writeField(wp, mHSN, 6);              // Hopping sequence number
	dest.writeField(wp, 0x0, 1);               // "0" RFL number list: Not present
	dest.writeField(wp, 0x0, 1);               // "0" MA_BITMAP: Present
	dest.writeField(wp, mMA_BITMAP_LENGTH, 6); // MA_BITMAP_LENGTH
	dest.writeField(wp, mMA_BITMAP, 4);        // MA_BITMAP

	dest.writeField(wp, 0x0, 1);                    // "0" PBCCH not present in Cell
	dest.writeField(wp, mRAC, 8);                   // RAC
	dest.writeField(wp, mSPGC_CCCH_SUP, 1);         // SPGC_CCCH_SUP
	dest.writeField(wp, mPRIORITY_ACCESS_THR, 3);   // PRIORITY_ACCESS_THR
	dest.writeField(wp, mNETWORK_CONTROL_ORDER, 2); // NETWORK_CONTROL_ORDER

	// GPRS Cell Options GSM 04.60 12.24 
	dest.writeField(wp, mNMO, 2);               // NMO Network Mode of Operation
	dest.writeField(wp, mT3168, 3);             // T3168
	dest.writeField(wp, mT3192, 3);             // T3192
	dest.writeField(wp, mDRXTIMER_MAX, 3);      // DRXTIMER_MAX
	dest.writeField(wp, mACCESS_BURST_TYPE, 1); // ACCESS_BURST_TYPE
	dest.writeField(wp, mCONTROL_ACK_TYPE, 1);  // Control Ack Type
	dest.writeField(wp, mBS_CV_MAX, 4);         // BS_CV_MAX
	dest.writeField(wp, 0x1, 1);                // "1" PAN description: Present
	dest.writeField(wp, mPAN_DEC, 3);           // PAN_DEC
	dest.writeField(wp, mPAN_INC, 3);           // PAN_INC
	dest.writeField(wp, mPAN_MAX, 3);           // PAN_MAX
	dest.writeField(wp, 0x0, 1);                // "0" Cell Options Extension Information: Not present

	// GPRS Power Control Parameters
	dest.writeField(wp, mALPHA, 4);         // ALPHA 
	dest.writeField(wp, mT_AVG_W, 5);       // T_AVG_W
	dest.writeField(wp, mT_AVG_T, 5);       // T_AVG_T              
	dest.writeField(wp, mPC_MEAS_CHAN, 1);  // PC_MEAS_CHAN
	dest.writeField(wp, mN_AVG_I, 4);       // N_AVG_I
	// spare padding
	dest.writeField(wp, 0x0b, 5);
	for (int i=0; i<8; i++)
		dest.writeField(wp,0x2b,8);
}

void L3SI13RestOctets::text(ostream& os) const
{
	os << " BCCH_CHANGE_MARK=" << mBCCH_CHANGE_MARK;
	os << " SI_CHANGE_FIELD=" << mSI_CHANGE_FIELD;
	os << " SI13_CHANGE_MARK=" << mSI13_CHANGE_MARK;
	os << " HSN=" << mHSN;
	os << " MA_BITMAP_LENGTH=" << mMA_BITMAP_LENGTH;
	os << " MA_BITMAP=" << mMA_BITMAP;
	os << " RAC=" << mRAC;
	os << " SPGC_CCCH_SUP=" << mSPGC_CCCH_SUP;
	os << " PRIORITY_ACCESS_THR=" << mPRIORITY_ACCESS_THR; 
	os << " NETWORK_CONTROL_ORDER=" << mNETWORK_CONTROL_ORDER;
	os << " NMO=" << mNMO;
	os << " T3168=" << mT3168;
	os << " T3192=" << mT3192;
	os << " DRXTIMER_MAX=" << mDRXTIMER_MAX;
	os << " ACCESS_BURST_TYPE=" << mACCESS_BURST_TYPE;
	os << " CONTROL_ACK_TYPE=" << mCONTROL_ACK_TYPE;
	os << " BS_CV_MAX=" << mBS_CV_MAX;
	os << " PAN_DEC=" << mPAN_DEC;
	os << " PAN_INC=" << mPAN_INC;
	os << " PAN_MAX=" << mPAN_MAX;
	os << " ALPHA=" << mALPHA;
	os << " T_AVG_W=" << mT_AVG_W;
	os << " T_AVG_T=" << mT_AVG_T;
	os << " PC_MEAS_CHAN=" << mPC_MEAS_CHAN;
	os << " N_AVG_I=" << mN_AVG_I;
}



void L3PageMode::writeV(L3Frame& dest, size_t &wp) const
{
	// PageMode is 1/2 octet. Spare[3:2], PM[1:0]
	dest.writeField(wp, 0x00, 2);
	dest.writeField(wp, mPageMode, 2);
}

void L3PageMode::parseV( const L3Frame &src, size_t &rp)
{
	// Read out spare bits.
	rp += 2;
	// Read out PageMode.
	mPageMode = src.readField(rp, 2);
} 



void L3PageMode::text(ostream& os) const
{
	os << mPageMode;
}


void L3DedicatedModeOrTBF::writeV( L3Frame& dest, size_t &wp )const
{
	// 1/2 Octet. 
	dest.writeField(wp, 0, 1);
	dest.writeField(wp, mTMA, 1);
	dest.writeField(wp, mDownlink, 1);
	dest.writeField(wp, mDMOrTBF, 1);
}


void L3DedicatedModeOrTBF::text(ostream& os) const
{
	os << "TMA=" << mTMA;
	os << " Downlink=" << mDownlink;
	os << " DMOrTBF=" << mDMOrTBF;
}


void L3ChannelDescription::writeV( L3Frame &dest, size_t &wp ) const 
{
	// GSM 04.08 10.5.2.5
// 					Channel Description Format (non-hopping)
// 		 	7      6      5      4      3     2      1      0
//	  [         TSC       ][ H=0 ][ SPARE(0,0)][ ARFCN[9:8] ]  Octet 3
//	  [                ARFCN[7:0]                           ]  Octet 4 H=0
//

	// HACK -- Hard code for non-hopping.
	assert(mHFlag==0);
	dest.writeField(wp,mTypeAndOffset,5);
	dest.writeField(wp,mTN,3);
	dest.writeField(wp,mTSC,3);
	dest.writeField(wp,0,3);				// H=0 + 2 spares
	dest.writeField(wp,mARFCN,10);
}



void L3ChannelDescription::parseV(const L3Frame& src, size_t &rp)
{
	// GSM 04.08 10.5.2.5
	mTypeAndOffset = (TypeAndOffset)src.readField(rp,5);
	mTN = src.readField(rp,3);
	mTSC = src.readField(rp,3);
	mHFlag = src.readField(rp,1);
	if (mHFlag) {
		mMAIO = src.readField(rp,6);
		mHSN = src.readField(rp,6);
	} else {
		rp += 2;	// skip 2 spare bits
		mARFCN = src.readField(rp,10);
	}
}


void L3ChannelDescription::text(std::ostream& os) const
{

	os << "typeAndOffset=" << mTypeAndOffset;
	os << " TN=" << mTN;
	os << " TSC=" << mTSC;
	os << " ARFCN=" << mARFCN;	
}



void L3RequestReference::writeV( L3Frame &dest, size_t &wp ) const 
{


// 					Request Reference Format.
// 		 	7      6      5      4      3     2      1      0
//	  [      			RequestReference [7:0]   			]  Octet 2
//	  [			T1[4:0]                 ][   T3[5:3]        ]  Octet 3
//	  [       T3[2:0]     ][            T2[4:0]             ]  Octet 4

	dest.writeField(wp, mRA, 8);
	dest.writeField(wp, mT1p, 5);
	dest.writeField(wp, mT3, 6);
	dest.writeField(wp, mT2, 5);
}


void L3RequestReference::text(ostream& os) const
{
	os << "RA=" << mRA;	
	os << " T1'=" << mT1p;
	os << " T2=" << mT2;
	os << " T3=" << mT3;	
}

void L3StartingTime::writeV( L3Frame &dest, size_t &wp ) const 
{


// 					Request Reference Format.
// 		 	7      6      5      4      3     2      1      0
//	  [			T1[4:0]                 ][   T3[5:3]        ]  Octet 2
//	  [       T3[2:0]     ][            T2[4:0]             ]  Octet 3

	dest.writeField(wp, mT1p, 5);
	dest.writeField(wp, mT3, 6);
	dest.writeField(wp, mT2, 5);
}


void L3StartingTime::text(ostream& os) const
{
	os << "T1'=" << mT1p;
	os << " T2=" << mT2;
	os << " T3=" << mT3;	
}


void L3TimingAdvance::writeV( L3Frame &dest, size_t &wp ) const
{
	dest.writeField(wp, 0x00, 2);
	dest.writeField(wp, mTimingAdvance, 6);
}

void L3TimingAdvance::text(ostream& os) const
{
    os << mTimingAdvance;
}

void L3IARestOctets::writeV(L3Frame& dest, size_t &wp) const
{
	// GMS 04.08 10.5.2.37b 10.5.2.16
	size_t rest_start = wp;
	dest.writeField(wp, 3, 2);                 // "HH"
	dest.writeField(wp, 0, 2);                 // "0" Packet Uplink Assignment
	dest.writeField(wp, mBLOCK_ALLOCATION, 1); // Block Allocation
	if (mBLOCK_ALLOCATION) {   
		// Not Single Block Allocation
		dest.writeField(wp, mTFI_ASSIGNMENT, 5);  // TFI_ASSIGNMENT Temporary Flow Identity
		dest.writeField(wp, mPOLLING, 1);         // POLLING
		dest.writeField(wp, mALLOCATION_TYPE, 1); // ALLOCATION_TYPE: dynamic or fixed
		if (!mALLOCATION_TYPE) {
			// Dynamic Allocation
			dest.writeField(wp, mUSF, 3);             // USF
			dest.writeField(wp, mUSF_GRANULARITY, 1); // USF_GRANULARITY
			dest.writeField(wp, 0 , 1);               // "0" power control: Not Present
		} else {
			// Fixed Allocation
			dest.writeField(wp, mALLOCATION_BITMAP_LENGTH, 5);                  // ALLOCATION_BITMAP_LENGTH
			dest.writeField(wp, mALLOCATION_BITMAP, mALLOCATION_BITMAP_LENGTH); // ALLOCATION_BITMAP
			dest.writeField(wp, 0, 1);                                          // "0" power control: Not Present
		} 
		dest.writeField(wp, mCHANNEL_CODING_COMMAND, 2);    // CHANNEL_CODING_COMMAND 
		dest.writeField(wp, mTLLI_BLOCK_CHANNEL_CODING, 1); // TLLI_BLOCK_CHANNEL_CODING
		dest.writeField(wp, 1 , 1);                         // "1" Alpha : Present
		dest.writeField(wp, mALPHA, 4);                     // Alpha
		dest.writeField(wp, mGAMMA, 5);                     // Gamma
		dest.writeField(wp, mTIMING_ADVANCE_INDEX_FLAG, 1); // TIMING_ADVANCE_INDEX_FLAG
		if (mTIMING_ADVANCE_INDEX_FLAG) {
			dest.writeField(wp, mTIMING_ADVANCE_INDEX, 4);	// TIMING_ADVANCE_INDEX
		}
		dest.writeField(wp, mTBF_STARTING_TIME_FLAG, 1);    // TBF_STARTING_TIME_FLAG
		if (mTBF_STARTING_TIME_FLAG) {
			L3StartingTime ie_time(mTBF_starting_time);
			ie_time.writeV(dest, wp);
		} 
	} else {
		//Single Block Allocation
		dest.writeField(wp, 1, 1);      // "1" Alpha : Present
		dest.writeField(wp, mALPHA, 4); // Alpha
		dest.writeField(wp, mGAMMA, 5); // Gamma
		dest.writeField(wp, 1, 2);      // "01"
		L3StartingTime ie_time(mTBF_starting_time);
		ie_time.writeV(dest, wp);  // mTBF_STARTING_TIME
		dest.writeField(wp, 0, 1); // "L" power control: Not Present
	} 

	size_t rest_len = wp - rest_start;
	assert(rest_len <= 11*8);
	size_t num_bits = rest_len%8;
	if (num_bits != 0) {
		dest.writeField(wp, 0x2b, 8-num_bits);  // Rest Octets padding
	}
	for (int i=0; i<11-(rest_len+8)/8; i++)
		dest.writeField(wp, 0x2b, 8); // Rest Octets padding

}

void L3IARestOctets::text(ostream& os) const
{
	os << " BLOCK_ALLOCATION=" << mBLOCK_ALLOCATION;
	os << " TFI_ASSIGNMENT=" << mTFI_ASSIGNMENT;
	os << " POLLING=" << mPOLLING;
	os << " ALLOCATION_TYPE=" << mALLOCATION_TYPE;
	os << " USF=" << mUSF;
	os << " USF_GRANULARITY=" << mUSF_GRANULARITY;
	os << " ALLOCATION_BITMAP_LENGTH=" << mALLOCATION_BITMAP_LENGTH;
	os << " ALLOCATION_BITMAP=" << mALLOCATION_BITMAP;
	os << " CHANNEL_CODING_COMMAND=" << mCHANNEL_CODING_COMMAND;
	os << " TLLI_BLOCK_CHANNEL_CODING=" << mTLLI_BLOCK_CHANNEL_CODING;
	os << " ALPHA=" << mALPHA;
	os << " GAMMA=" << mGAMMA;
	os << " TIMING_ADVANCE_INDEX_FLAG=" << mTIMING_ADVANCE_INDEX_FLAG;
	os << " TIMING_ADVANCE_INDEX=" << mTIMING_ADVANCE_INDEX;
	os << " TBF_STARTING_TIME_FLAG=" << mTBF_STARTING_TIME_FLAG;
	os << " TBF_starting_time=" << mTBF_starting_time;
}

void L3IARestOctetsDownlinkAssignment::writeV(L3Frame& dest, size_t &wp) const
{
	for(unsigned i = 0; i < 7; i++)
		dest.writeField(wp, mIARestOctets[i], 8);
	L3StartingTime ie_time(mTBF_starting_time);
	ie_time.writeV(dest, wp);  // mTBF_STARTING_TIME
	dest.writeField(wp, mIARestOctets[9], 8);
	dest.writeField(wp, mIARestOctets[10], 8);
}
void L3IARestOctetsDownlinkAssignment::text(ostream& os) const
{
	os << " TBF_starting_time=" << mTBF_starting_time;
}

void L3RRCause::writeV( L3Frame &dest, size_t &wp ) const
{
	dest.writeField(wp, mCauseValue, 8);	
}

void L3RRCause::parseV( const L3Frame &src, size_t &rp )
{
	mCauseValue = src.readField(rp, 8);
}

void L3RRCause::text(ostream& os) const
{
	os << "0x" << hex << mCauseValue << dec;
}




void L3PowerCommand::writeV( L3Frame &dest, size_t &wp )const
{
	dest.writeField(wp, 0, 3);
	dest.writeField(wp, mCommand, 5);
}


void L3PowerCommand::text(ostream& os) const
{
	os << mCommand;
}





void L3ChannelMode::writeV( L3Frame& dest, size_t &wp) const
{
	dest.writeField(wp, mMode, 8);
}

void L3ChannelMode::parseV(const L3Frame& src, size_t& rp)
{
	mMode = (Mode)src.readField(rp,8);
}



ostream& GSM::operator<<(ostream& os, L3ChannelMode::Mode mode)
{
	switch (mode) {
		case L3ChannelMode::SignallingOnly: os << "signalling"; break;
		case L3ChannelMode::SpeechV1: os << "speech1"; break;
		case L3ChannelMode::SpeechV2: os << "speech2"; break;
		case L3ChannelMode::SpeechV3: os << "speech3"; break;
		default: os << "?" << (int)mode << "?";
	}
	return os;
}

void L3ChannelMode::text(ostream& os) const
{
	os << mMode;
}

// Application Information IEs

// APDU ID

void L3APDUID::writeV( L3Frame& dest, size_t &wp) const
{
	// APDU ID is 1/2 octet. Protocol Identifier [3:0]
    dest.writeField(wp,mProtocolIdentifier,4);
}

void L3APDUID::parseV( const L3Frame &src, size_t &rp)
{
	// Read out Protocol Identifier.
	mProtocolIdentifier = src.readField(rp, 4);
}

void L3APDUID::text(ostream& os) const
{
	os << mProtocolIdentifier;
}

// APDU Flags

void L3APDUFlags::writeV( L3Frame& dest, size_t &wp) const
{
	// APDU Flags is 1/2 octet. Protocol Identifier [3:0]
    dest.writeField(wp,0,1); // spare
    dest.writeField(wp,mCR,1); // C/R
    dest.writeField(wp,mFirstSegment,1);
    dest.writeField(wp,mLastSegment,1);
}

void L3APDUFlags::parseV( const L3Frame &src, size_t &rp)
{
	// Read out Protocol Identifier.
    rp += 1; // skip spare
	mCR = src.readField(rp, 1);
	mFirstSegment = src.readField(rp, 1);
	mLastSegment = src.readField(rp, 1);
}

void L3APDUFlags::text(ostream& os) const
{
	os << mCR << "," << mFirstSegment << "," << mLastSegment;
}

// APDU Data

L3APDUData::~L3APDUData()
{
}

L3APDUData::L3APDUData()
    :L3ProtocolElement()
{
}

L3APDUData::L3APDUData(BitVector data)
    :L3ProtocolElement()
    ,mData(data)
{
}



void L3APDUData::writeV( L3Frame& dest, size_t &wp) const
{
    // we only need to write the data part
    // TODO - single line please. copy / memcpy, anything better then a for loop
    LOG(DEBUG) << "L3APDUData: writeV " << mData.size() << " bits";
    mData.copyToSegment(dest, wp);
    wp += mData.size() / 8;
}

void L3APDUData::parseV( const L3Frame& src, size_t &rp, size_t expectedLength )
{
    LOG(DEBUG) << "L3APDUData: parseV " << expectedLength << " bytes";
    mData.resize(expectedLength*8);
    src.copyToSegment(mData, rp, expectedLength*8); // expectedLength is bytes, not bits
    //for ( size_t i = 0 ; i < expectedLength ; ++i)
    //    mData[i] = src.readField(rp, 8);
}

void L3APDUData::text(ostream& os) const
{
    // TODO - use the following two lines (get rid of the "char / char*" error)
    //std::ostream_iterator<std::string> output( os, "" );
    //std::copy( mData.begin(), mData.end(), output );
    for (size_t i = 0 ; i < mData.size() ; ++i) {
        os << (mData[i] ? "1" : "0");
	}

}


void L3MeasurementResults::parseV(const L3Frame& frame, size_t &rp)
{
	// GSM 04.08 10.5.2.20
	mBA_USED = frame.readField(rp,1);
	mDTX_USED = frame.readField(rp,1);
	mRXLEV_FULL_SERVING_CELL = frame.readField(rp,6);
	rp++;	// spare
	mMEAS_VALID = frame.readField(rp,1);
	mRXLEV_SUB_SERVING_CELL = frame.readField(rp,6);
	rp++;	// spare
	mRXQUAL_FULL_SERVING_CELL = frame.readField(rp,3);
	mRXQUAL_SUB_SERVING_CELL = frame.readField(rp,3);
	mNO_NCELL = frame.readField(rp,3);
	for (unsigned i=0; i<6; i++) {
		mRXLEV_NCELL[i] = frame.readField(rp,6);
		mBCCH_FREQ_NCELL[i] = frame.readField(rp,5);
		mBSIC_NCELL[i] = frame.readField(rp,6);
	}
}


void L3MeasurementResults::text(ostream& os) const
{
	// GSM 04.08 10.5.2.20
	os << "BA_USED=" << mBA_USED;
	os << " DTX_USED=" << mDTX_USED;
	os << " MEAS_VALID=" << mMEAS_VALID;
	// Note that the value of the MEAS-VALID bit is reversed
	// from what you might expect.
	if (mMEAS_VALID) return;
	os << " RXLEV_FULL_SERVING_CELL=" << mRXLEV_FULL_SERVING_CELL;
	os << " RXLEV_SUB_SERVING_CELL=" << mRXLEV_SUB_SERVING_CELL;
	os << " RXQUAL_FULL_SERVING_CELL=" << mRXQUAL_FULL_SERVING_CELL;
	os << " RXQUAL_SUB_SERVING_CELL=" << mRXQUAL_SUB_SERVING_CELL;
	os << " NO_NCELL=" << mNO_NCELL;
	// no measurements?
	if (mNO_NCELL==0) return;
	// no neighbor list?
	if (mNO_NCELL==7) return;
	for (unsigned i=0; i<mNO_NCELL; i++) {
		os << " RXLEV_NCELL" << i+1 << "=" << mRXLEV_NCELL[i];
		os << " BCCH_FREQ_NCELL" << i+1 << "=" << mBCCH_FREQ_NCELL[i];
		os << " BSIC_NCELL" << i+1 << "=" << mBSIC_NCELL[i];
	}
}


unsigned L3MeasurementResults::RXLEV_NCELL(unsigned * target) const
{
	for (unsigned i=0; i<mNO_NCELL; i++) target[i] = mRXLEV_NCELL[i];
	return mNO_NCELL;
}


unsigned L3MeasurementResults::BCCH_FREQ_NCELL(unsigned * target) const
{
	for (unsigned i=0; i<mNO_NCELL; i++) target[i] = mBCCH_FREQ_NCELL[i];
	return mNO_NCELL;
}


unsigned L3MeasurementResults::BSIC_NCELL(unsigned * target) const
{
	for (unsigned i=0; i<mNO_NCELL; i++) target[i] = mBSIC_NCELL[i];
	return mNO_NCELL;
}


int L3MeasurementResults::decodeLevToDBm(unsigned lev) const
{
	// See GSM 05.08 8.1.4.
	// SCALE is 0 for anything but the ENHANCED MEASUREMENT REPORT message.
	return -111 + lev;
}

float L3MeasurementResults::decodeQualToBER(unsigned qual) const
{
	// See GSM 05.08 8.2.4.
	// Convert lowest value as "0" instead of 0.14%.
	static const float vals[] = {0.0, 0.28, 0.57, 1.13, 2.26, 4.53, 9.05, 18.10};
	assert(qual<8);
	return 0.01*vals[qual];
}



/*
L3SI3RestOctets::L3SI3RestOctets()
	:L3RestOctets(),
	mHaveSelectionParameters(false),
	mCBQ(0),mCELL_RESELECT_OFFSET(0),
	mTEMPORARY_OFFSET(0),
	mPENALTY_TIME(0)
{
	// See GSM 04.08 10.5.2.34 and 05.08 9 Table 1.
	if (!gConfig.defines("GSM.SI3RO")) return;

	// Optional Cell Selection Parameters.

	// CELL_BAR_QUALIFY. 1 bit. Default value is 0.
	if (gConfig.defines("GSM.SI3RO.CBQ")) {
		mCBQ = gConfig.getNum("GSM.SI3RO.CBQ");
		mHaveSelectionParameters = true;
	}
	// CELL_RESELECT_OFFSET. 6 bits. Default value is 0.
	// C2 offset in 2 dB steps
	if (gConfig.defines("GSM.SI3RO.CRO")) {
		mCELL_RESELECT_OFFSET = gConfig.getNum("GSM.SI3RO.CRO");
		mHaveSelectionParameters = true;
	}
	// Another offset to C2 in 10 dB steps, applied during penalty time.
	// 3 bits.  // Default is 0 dB but "7" means "infinity".
	if (gConfig.defines("GSM.SI3RO.TEMPORARY_OFFSET")) {
		mTEMPORARY_OFFSET = gConfig.getNum("GSM.SI3RO.TEMPORARY_OFFSET");
		mHaveSelectionParameters = true;
	}
	// The time for which the temporary offset is applied, 20*(n+1).
	if (gConfig.defines("GSM.SI3RO.PENALTY_TIME")) {
		mPENALTY_TIME = gConfig.getNum("GSM.SI3RO.PENALTY_TIME");
		mHaveSelectionParameters = true;
	}
}


size_t L3SI3RestOctets::lengthV() const
{
	size_t sumBits = 0;
	if (mHaveSelectionParameters) sumBits += 1 + 1+6+3+5;

	size_t octets = sumBits/8;
	if (sumBits%8) octets += 1;
	return octets;
}


void L3SI3RestOctets::writeV(L3Frame& dest, size_t &wp) const
{
	if (mHaveSelectionParameters) {
		dest.writeH(wp);
		dest.writeField(wp,mCBQ,1);
		dest.writeField(wp,mCELL_RESELECT_OFFSET,6);
		dest.writeField(wp,mTEMPORARY_OFFSET,3);
		dest.writeField(wp,mPENALTY_TIME,5);
	}
}


void L3SI3RestOctets::text(ostream& os) const
{
	if (mHaveSelectionParameters) {
		os << "CBQ=" << mCBQ;
		os << " CELL_RESELECT_OFFSET=" << mCELL_RESELECT_OFFSET;
		os << " TEMPORARY_OFFSET=" << mTEMPORARY_OFFSET;
		os << " PANALTY_TIME=" << mPENALTY_TIME;
	}
}
*/


// vim: ts=4 sw=4
