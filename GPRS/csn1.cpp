/* csn1.cpp
 * Routines for CSN1 dissection in wireshark.
 *
 * Copyright (C) 2011 Ivan Klyuchnikov
 *
 * By Vincent Helfre, based on original code by Jari Sassi
 * with the gracious authorization of STE
 * Copyright (c) 2011 ST-Ericsson
 *
 * $Id: packet-csn1.c 39140 2011-09-25 22:01:50Z wmeier $
 *
 * Wireshark - Network traffic analyzer
 * By Gerald Combs <gerald@wireshark.org>
 * Copyright 1998 Gerald Combs
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 */

#include <iostream>
#include <cstdlib>
#include "csn1.h"


#define pvDATA(_pv, _offset) ((void*) ((unsigned char*)_pv + _offset))
#define pui8DATA(_pv, _offset) ((guint8*) pvDATA(_pv, _offset))
#define pui16DATA(_pv, _offset) ((guint16*) pvDATA(_pv, _offset))
#define pui32DATA(_pv, _offset) ((guint32*) pvDATA(_pv, _offset))
#define pui64DATA(_pv, _offset) ((guint64*) pvDATA(_pv, _offset))
/* used to tag existence of next element in variable length lists */
#define STANDARD_TAG 1
#define REVERSED_TAG 0
#define LOG(INFO) cout

using namespace std;
static const unsigned char ixBitsTab[] = {0, 1, 1, 2, 2, 3, 3, 3, 4, 4, 4, 4, 4, 4, 4, 4, 5};


/* Returns no_of_bits (up to 8) masked with 0x2B */

static guint8
get_masked_bits8( BitVector *vector, size_t& readIndex, gint bit_offset,  const gint no_of_bits)
{
  static const guint8 maskBits[] = {0x00, 0x01, 0x03, 0x07, 0x0F, 0x1F, 0x3F, 0x7F, 0xFF};
  //gint byte_offset = bit_offset >> 3;          /* divide by 8 */
  gint relative_bit_offset = bit_offset & 0x07;  /* modulo 8 */
  guint8 result;
  gint bit_shift = 8 - relative_bit_offset - (gint) no_of_bits;
  readIndex -= relative_bit_offset;
  if (bit_shift >= 0)
  {
    result = (0x2B ^ ((guint8)vector->readField(readIndex, 8))) >> bit_shift;
    readIndex-= bit_shift;
    result &= maskBits[no_of_bits];
  }
  else
  { 
    guint8 hight_part = (0x2B ^ ((guint8)vector->readField(readIndex, 8))) & maskBits[8 - relative_bit_offset];
    hight_part = (guint8) (hight_part << (-bit_shift));
    result =  (0x2B ^ ((guint8)vector->readField(readIndex, 8))) >> (8 + bit_shift);
    readIndex = readIndex - (8 - (-bit_shift));
    result |= hight_part;
  }
  return result;
}

/**
 * ================================================================================================
 * set initial/start values in help data structure used for packing/unpacking operation
 * ================================================================================================
 */
void
csnStreamInit(csnStream_t* ar, gint bit_offset, gint remaining_bits_len)
{
  ar->remaining_bits_len  = remaining_bits_len;
  ar->bit_offset          = bit_offset;
}

static const char* ErrCodes[] =
{
  "General 0",
  "General -1",
  "DATA_NOT VALID",
  "IN SCRIPT",
  "INVALID UNION INDEX",
  "NEED_MORE BITS TO UNPACK",
  "ILLEGAL BIT VALUE",
  "Internal",
  "STREAM_NOT_SUPPORTED",
  "MESSAGE_TOO_LONG"
};

static gint16
ProcessError( size_t readIndex, const char* sz, gint16 err, const CSN_DESCR* pDescr)
{
  gint16 i = MIN(-err, ((gint16) ElementsOf(ErrCodes)-1));
  if (i >= 0)
  {
    LOG(ERR) << sz << "Error code: "<< ErrCodes[i] << pDescr?(pDescr->sz):"-";
  }
  else
  {
    LOG(ERR) << sz << ": " << pDescr?(pDescr->sz):"-";
  }
  return err;
}

//#if 0
static const char* CSN_DESCR_type[]=
{
  "CSN_END",
  "CSN_BIT",
  "CSN_UINT",
  "CSN_TYPE",
  "CSN_CHOICE",
  "CSN_UNION",
  "CSN_UNION_LH",
  "CSN_UINT_ARRAY",
  "CSN_TYPE_ARRAY",
  "CSN_BITMAP",
  "CSN_VARIABLE_BITMAP",
  "CSN_VARIABLE_BITMAP_1",
  "CSN_LEFT_ALIGNED_VAR_BMP",
  "CSN_LEFT_ALIGNED_VAR_BMP_1",
  "CSN_VARIABLE_ARRAY",
  "CSN_VARIABLE_TARRAY",
  "CSN_VARIABLE_TARRAY_OFFSET",
  "CSN_RECURSIVE_ARRAY",
  "CSN_RECURSIVE_TARRAY",
  "CSN_RECURSIVE_TARRAY_1",
  "CSN_RECURSIVE_TARRAY_2",
  "CSN_EXIST",
  "CSN_EXIST_LH",
  "CSN_NEXT_EXIST",
  "CSN_NEXT_EXIST_LH",
  "CSN_NULL",
  "CSN_FIXED",
  "CSN_CALLBACK",
  "CSN_UINT_OFFSET",
  "CSN_UINT_LH",
  "CSN_SERIALIZE",
  "CSN_TRAP_ERROR"
  "CSN_???"
};
//#endif

/**
 * ================================================================================================
 * Return TRUE if tag in bit stream indicates existence of next list element,
 * otherwise return FALSE.
 * Will work for tag values equal to both 0 and 1.
 * ================================================================================================
 */

static gboolean
existNextElement(BitVector *vector, size_t& readIndex, guint8 Tag)
{
  guint8 res = vector->readField(readIndex, 1);
  //LOG(INFO) << "EXIST TAG = " << (unsigned)res;
  if (Tag == STANDARD_TAG)
  {
    return (res > 0);
  }
  return (res == 0);
}


gint16
csnStreamDecoder(csnStream_t* ar, const CSN_DESCR* pDescr, BitVector *vector, size_t& readIndex, void* data)
{
  gint  remaining_bits_len = ar->remaining_bits_len;
  gint  bit_offset         = ar->bit_offset;
  guint8*  pui8;
  guint16* pui16;
  guint32* pui32;
  guint64* pui64;
  guint8 Tag = STANDARD_TAG;

  if (remaining_bits_len <= 0)
  {
    return 0;
  }

  do
  {
    switch (pDescr->type)
    {
      case CSN_BIT:
      {
        if (remaining_bits_len > 0)
        {
          pui8  = pui8DATA(data, pDescr->offset);
          *pui8 = vector->readField(readIndex, 1);
          LOG(INFO) << pDescr->sz << " = " <<(unsigned)*pui8 << "\n";
          /* end add the bit value to protocol tree */
        }
        else
        {
          return ProcessError(readIndex,"csnStreamDecoder", CSN_ERROR_NEED_MORE_BITS_TO_UNPACK, pDescr);
        }

        pDescr++;
        remaining_bits_len--;
        bit_offset++;
        break;
      }

      case CSN_NULL:
      { /* Empty member! */
        pDescr++;
        break;
      }

      case CSN_UINT:
      {
        guint8 no_of_bits = (guint8) pDescr->i;

        if (remaining_bits_len >= no_of_bits)
        {
          if (no_of_bits <= 8)
          {
            guint8 ui8 = vector->readField(readIndex, no_of_bits);
            pui8      = pui8DATA(data, pDescr->offset);
            *pui8     = ui8;
            LOG(INFO) << pDescr->sz << " = " << (unsigned)*pui8 << "\n";
          }
          else if (no_of_bits <= 16)
          {
            guint16 ui16 = vector->readField(readIndex, no_of_bits);
            pui16       = pui16DATA(data, pDescr->offset);
            *pui16      = ui16;
            LOG(INFO) << pDescr->sz << " = " << *pui16 << "\n";;
          }
          else if (no_of_bits <= 32)
          {
            guint32 ui32 = vector->readField(readIndex, no_of_bits);
            pui32       = pui32DATA(data, pDescr->offset);
            *pui32      = ui32;
            LOG(INFO) << pDescr->sz << " = " << *pui32 << "\n";
          }
          else
          {
            return ProcessError(readIndex,"csnStreamDecoder", CSN_ERROR_GENERAL, pDescr);
          }
        }
        else
        {
          return ProcessError(readIndex,"csnStreamDecoder", CSN_ERROR_NEED_MORE_BITS_TO_UNPACK, pDescr);
        }

        remaining_bits_len -= no_of_bits;
        bit_offset += no_of_bits;
        pDescr++;
        break;
      }

      case CSN_UINT_OFFSET:
      {
        guint8 no_of_bits = (guint8) pDescr->i;

        if (remaining_bits_len >= no_of_bits)
        {
          if (no_of_bits <= 8)
          {
            guint8 ui8 = vector->readField(readIndex, no_of_bits);
            pui8      = pui8DATA(data, pDescr->offset);
            *pui8     = ui8 + (guint8)pDescr->descr.value;
            LOG(INFO) << pDescr->sz << " = " << (unsigned)*pui8 << "\n";
          }
          else if (no_of_bits <= 16)
          {
            guint16 ui16 = vector->readField(readIndex, no_of_bits);
            pui16       = pui16DATA(data, pDescr->offset);
            *pui16      = ui16 + (guint16)pDescr->descr.value;
            LOG(INFO) << pDescr->sz << " = " << *pui16 << "\n";;
          }
          else if (no_of_bits <= 32)
          {
            guint32 ui32 = vector->readField(readIndex, no_of_bits);
            pui32       = pui32DATA(data, pDescr->offset);
            *pui32      = ui32 + (guint16)pDescr->descr.value;
            LOG(INFO) << pDescr->sz << " = " << *pui32 << "\n";
          }
          else
          {
            return ProcessError(readIndex,"csnStreamDecoder", CSN_ERROR_GENERAL, pDescr);
          }
        }
        else
        {
          return ProcessError(readIndex,"csnStreamDecoder", CSN_ERROR_NEED_MORE_BITS_TO_UNPACK, pDescr);
        }

        remaining_bits_len -= no_of_bits;
        bit_offset += no_of_bits;
        pDescr++;
        break;
      }

      case CSN_UINT_LH:
      {
        guint8 no_of_bits = (guint8) pDescr->i;

        if (remaining_bits_len >= no_of_bits)
        {
          remaining_bits_len -= no_of_bits;
          if (no_of_bits <= 8)
          {
            guint8 ui8 = get_masked_bits8(vector, readIndex, bit_offset, no_of_bits);
            pui8      = pui8DATA(data, pDescr->offset);
            *pui8     = ui8;
            LOG(INFO) << pDescr->sz << " = " << (unsigned)*pui8 << "\n";
          }
          else
          {/* Maybe we should support more than 8 bits ? */
            return ProcessError(readIndex,"csnStreamDecoder", CSN_ERROR_GENERAL, pDescr);
          }
        }
        else
        {
          return ProcessError(readIndex,"csnStreamDecoder", CSN_ERROR_NEED_MORE_BITS_TO_UNPACK, pDescr);
        }

        remaining_bits_len -= no_of_bits;
        bit_offset += no_of_bits;
        pDescr++;
        break;
      }

      case CSN_UINT_ARRAY:
      {
        guint8  no_of_bits  = (guint8) pDescr->i;
        guint16 nCount = (guint16)pDescr->descr.value; /* nCount supplied by value i.e. M_UINT_ARRAY(...) */

        if (pDescr->serialize.value != 0)
        { /* nCount specified by a reference to field holding value i.e. M_VAR_UINT_ARRAY(...) */
          nCount = *pui16DATA(data, nCount);
        }

        if (remaining_bits_len >= no_of_bits)
        {
          remaining_bits_len -= (no_of_bits*nCount);
          if (no_of_bits <= 8)
          {
            pui8 = pui8DATA(data, pDescr->offset);
            do
            {
              *pui8 = vector->readField(readIndex, no_of_bits);
              LOG(INFO) << pDescr->sz << " = " << (unsigned)*pui8 << "\n";
              pui8++;
              bit_offset += no_of_bits;
            } while (--nCount > 0);
          }
          else if (no_of_bits <= 16)
          {
            return ProcessError(readIndex,"csnStreamDecoder NOTIMPLEMENTED", 999, pDescr);
          }
          else if (no_of_bits <= 32)
          {
            return ProcessError(readIndex,"csnStreamDecoder NOTIMPLEMENTED", 999, pDescr);
          }
          else
          {
            return ProcessError(readIndex,"csnStreamDecoder", CSN_ERROR_GENERAL, pDescr);
          }
        }
        else
        {
          return ProcessError(readIndex,"csnStreamDecoder", CSN_ERROR_NEED_MORE_BITS_TO_UNPACK, pDescr);
        }
        pDescr++;
        break;
      }

      case CSN_VARIABLE_TARRAY_OFFSET:
      case CSN_VARIABLE_TARRAY:
      case CSN_TYPE_ARRAY:
      {
        gint16      Status;
        csnStream_t arT    = *ar;
        gint16      nCount = pDescr->i;
        guint16      nSize  = (guint16)(gint32)pDescr->serialize.value;

        pui8 = pui8DATA(data, pDescr->offset);
        if (pDescr->type == CSN_VARIABLE_TARRAY)
        { /* Count specified in field */
          nCount = *pui8DATA(data, pDescr->i);
        }
        else if (pDescr->type == CSN_VARIABLE_TARRAY_OFFSET)
        { /* Count specified in field */
          nCount = *pui8DATA(data, pDescr->i);
	  /*  nCount--; the 1 offset is already taken into account in CSN_UINT_OFFSET */
        }

        while (nCount > 0)
        { /* resulting array of length 0 is possible
           * but no bits shall be read from bitstream
           */

          LOG(INFO) << pDescr->sz << "\n";
          csnStreamInit(&arT, bit_offset, remaining_bits_len);
          Status = csnStreamDecoder(&arT, (const CSN_DESCR*)pDescr->descr.ptr, vector, readIndex, pui8);
          if (Status >= 0)
          {
            pui8    += nSize;
            remaining_bits_len = arT.remaining_bits_len;
            bit_offset         = arT.bit_offset;
          }
          else
          {
            return Status;
          }
          nCount--;
        }

        pDescr++;
        break;
      }

      case CSN_BITMAP:
      { /* bitmap with given length. The result is left aligned! */
        guint8 no_of_bits = (guint8) pDescr->i; /* length of bitmap */

        if (no_of_bits > 0)
        {

          if (no_of_bits <= 32)
          {
            guint32 ui32 = vector->readField(readIndex, no_of_bits);
            pui32       = pui32DATA(data, pDescr->offset);
            *pui32      = ui32;
            LOG(INFO) << pDescr->sz << " = " << *pui32 << "\n";
          }
          else if (no_of_bits <= 64)
          {
            guint64 ui64 = vector->readField(readIndex, no_of_bits);
            pui64       = pui64DATA(data, pDescr->offset);
            *pui64      = ui64;
            LOG(INFO) << pDescr->sz << " = " << *pui64 << "\n";
          }
          else
          {
          	return ProcessError(readIndex,"csnStreamDecoder NOT IMPLEMENTED", 999, pDescr);
          }

          remaining_bits_len -= no_of_bits;
          assert(remaining_bits_len >= 0);
          bit_offset += no_of_bits;
        }
        /* bitmap was successfully extracted or it was empty */

        pDescr++;
        break;
      }

      case CSN_TYPE:
      {
        gint16      Status;
        csnStream_t arT = *ar;
        LOG(INFO) << ": " << pDescr->sz << "\n";
        csnStreamInit(&arT, bit_offset, remaining_bits_len);
        Status = csnStreamDecoder(&arT, (const CSN_DESCR*)pDescr->descr.ptr, vector, readIndex, pvDATA(data, pDescr->offset));
        LOG(INFO) << ": End " << pDescr->sz << "\n";
        if (Status >= 0)
        {
          remaining_bits_len  = arT.remaining_bits_len;
          bit_offset          = arT.bit_offset;
          pDescr++;
        }
        else
        {
          /* Has already been processed: ProcessError("csnStreamDecoder", Status, pDescr);  */
          return Status;
        }

        break;
      }

      case CSN_CHOICE:
      {
        gint16 count = pDescr->i;
        guint8 i     = 0;
        CSN_ChoiceElement_t* pChoice = (CSN_ChoiceElement_t*) pDescr->descr.ptr;

        while (count > 0)
        {
          guint8 no_of_bits = pChoice->bits;
          guint8 value = vector->readField(readIndex, no_of_bits);
          if (value == pChoice->value)
          {
            CSN_DESCR   descr[2];
            gint16      Status;
            csnStream_t arT = *ar;

            descr[0]      = pChoice->descr;
            memset(&descr[1], 0x00, sizeof(CSN_DESCR));
            descr[1].type = CSN_END;
            pui8          = pui8DATA(data, pDescr->offset);
            *pui8         = i;
            LOG(INFO) << "Choice " << pDescr->sz << " = " << (unsigned)value << "\n";
            bit_offset += no_of_bits;
            remaining_bits_len -= no_of_bits;

            csnStreamInit(&arT, bit_offset, remaining_bits_len);
            Status = csnStreamDecoder(&arT, descr, vector, readIndex, data);

            if (Status >= 0)
            {
              remaining_bits_len = arT.remaining_bits_len;
              bit_offset         = arT.bit_offset;
            }
            else
            {
              return Status;
            }
            break;
          }

          readIndex -= no_of_bits;
          count--;
          pChoice++;
          i++;
        }

        pDescr++;
        break;
      }

      case CSN_SERIALIZE:
      {
        StreamSerializeFcn_t serialize = pDescr->serialize.fcn;
        csnStream_t          arT       = *ar;
        gint16               Status    = -1;

        LOG(INFO) << pDescr->sz << " length = " << vector->readField(readIndex, 7) << "\n";
        arT.direction = 1;
        bit_offset += 7;
        remaining_bits_len -= 7;

        csnStreamInit(&arT, bit_offset, remaining_bits_len);
        Status = serialize(&arT, vector, readIndex, pvDATA(data, pDescr->offset));

        if (Status >= 0)
        {
          remaining_bits_len = arT.remaining_bits_len;
          bit_offset         = arT.bit_offset;
          pDescr++;
        }
        else
        {
          /* Has already been processed: */
          return Status;
        }

        break;
      }

      case CSN_UNION_LH:
      case CSN_UNION:
      {
        gint16           Bits;
        guint8           index;
        gint16           count      = pDescr->i;
        const CSN_DESCR* pDescrNext = pDescr;

        pDescrNext += count + 1; /* now this is next after the union */
        if ((count <= 0) || (count > 16))
        {
          return ProcessError(readIndex,"csnStreamDecoder", CSN_ERROR_INVALID_UNION_INDEX, pDescr);
        }

        /* Now get the bits to extract the index */
        Bits = ixBitsTab[count];
        index = 0;

        while (Bits > 0)
        {
          index <<= 1;

          if (CSN_UNION_LH == pDescr->type)
          {
            index |= get_masked_bits8(vector,readIndex, bit_offset, 1);
          }
          else
          {
            index |= vector->readField(readIndex, 1);
          }
          remaining_bits_len--;
          bit_offset++;
          Bits--;
        }

        /* Assign UnionType */
        pui8  = pui8DATA(data, pDescr->offset);
        *pui8 = index;
       

        /* script index to continue on, limited in case we do not have a power of 2 */
        pDescr += (MIN(index + 1, count));
        LOG(INFO) << pDescr->sz << " = " << (unsigned)*pui8 << "\n";

        switch (pDescr->type)
        { /* get the right element of the union based on computed index */

          case CSN_BIT:
          {
            pui8  = pui8DATA(data, pDescr->offset);
            *pui8 = 0x00;
            if (vector->readField(readIndex, 1) > 0)
            {
              *pui8 = 0x01;
            }
            LOG(INFO) << pDescr->sz << " = " << (unsigned)*pui8 << "\n";
            remaining_bits_len -= 1;
            bit_offset++;
            pDescr++;
            break;
          }

          case CSN_NULL:
          { /* Empty member! */
            pDescr++;
            break;
          }

          case CSN_UINT:
          {
            guint8 no_of_bits = (guint8) pDescr->i;
            if (remaining_bits_len >= no_of_bits)
            {
              remaining_bits_len -= no_of_bits;

              if (no_of_bits <= 8)
              {
                guint8 ui8 = vector->readField(readIndex,  no_of_bits);
                pui8       = pui8DATA(data, pDescr->offset);
                *pui8      = ui8;
                LOG(INFO) << pDescr->sz << " = " << (unsigned)*pui8 << "\n";
              }
              else if (no_of_bits <= 16)
              {
                guint16 ui16 = vector->readField(readIndex,  no_of_bits);
                pui16        = pui16DATA(data, pDescr->offset);
                *pui16       = ui16;
                LOG(INFO) << pDescr->sz << " = " << *pui16 << "\n";
              }
              else if (no_of_bits <= 32)
              {
                guint32 ui32 = vector->readField(readIndex,  no_of_bits);
                pui32       = pui32DATA(data, pDescr->offset);
                *pui32      = ui32;
                LOG(INFO) << pDescr->sz << " = " << *pui32 << "\n";
              }
              else
              {
                return ProcessError(readIndex,"csnStreamDecoder", CSN_ERROR_GENERAL, pDescr);
              }
            }
            else
            {
              return ProcessError(readIndex,"csnStreamDecoder", CSN_ERROR_GENERAL, pDescr);
            }

            bit_offset += no_of_bits;
            pDescr++;
            break;
          }

          case CSN_UINT_OFFSET:
          {
            guint8 no_of_bits = (guint8) pDescr->i;

            if (remaining_bits_len >= no_of_bits)
            {
              if (no_of_bits <= 8)
              {
                guint8 ui8 = vector->readField(readIndex,  no_of_bits);
                pui8      = pui8DATA(data, pDescr->offset);
                *pui8     = ui8 + (guint8)pDescr->descr.value;
                LOG(INFO) << pDescr->sz << " = " << (unsigned)*pui8 << "\n";
              }
              else if (no_of_bits <= 16)
              {
                guint16 ui16 = vector->readField(readIndex,  no_of_bits);
                pui16       = pui16DATA(data, pDescr->offset);
                *pui16      = ui16 + (guint16)pDescr->descr.value;
                LOG(INFO) << pDescr->sz << " = " << *pui16 << "\n";
              }
              else if (no_of_bits <= 32)
              {
                guint32 ui32 = vector->readField(readIndex,  no_of_bits);
                pui32       = pui32DATA(data, pDescr->offset);
                *pui32      = ui32 + (guint16)pDescr->descr.value;
                LOG(INFO) << pDescr->sz << " = " << *pui32 << "\n";
              }
              else
              {
                return ProcessError(readIndex,"csnStreamDecoder", CSN_ERROR_GENERAL, pDescr);
              }
            }
            else
            {
              return ProcessError(readIndex,"csnStreamDecoder", CSN_ERROR_NEED_MORE_BITS_TO_UNPACK, pDescr);
            }

            bit_offset += no_of_bits;
            pDescr++;
            break;
          }

          case CSN_UINT_LH:
          {
            guint8 no_of_bits = (guint8) pDescr->i;

            if (remaining_bits_len >= no_of_bits)
            {
              if (no_of_bits <= 8)
              {
                guint8 ui8 = get_masked_bits8(vector, readIndex, bit_offset, no_of_bits);
                pui8      = pui8DATA(data, pDescr->offset);
                *pui8     = ui8;
                LOG(INFO) << pDescr->sz << " = " << (unsigned)*pui8 << "\n";
              }
              else
              { /* Maybe we should support more than 8 bits ? */
                return ProcessError(readIndex,"csnStreamDecoder", CSN_ERROR_GENERAL, pDescr);
              }
            }
            else
            {
              return ProcessError(readIndex,"csnStreamDecoder", CSN_ERROR_NEED_MORE_BITS_TO_UNPACK, pDescr);
            }

            bit_offset += no_of_bits;
            pDescr++;
            break;
          }

          case CSN_UINT_ARRAY:
          {
            guint8  no_of_bits  = (guint8) pDescr->i;
            guint16 nCount = (guint16)pDescr->descr.value; /* nCount supplied by value i.e. M_UINT_ARRAY(...) */

            if (pDescr->serialize.value != 0)
            { /* nCount specified by a reference to field holding value i.e. M_VAR_UINT_ARRAY(...) */
              nCount = *pui16DATA(data, nCount);
            }

            if (remaining_bits_len >= no_of_bits)
            {
              remaining_bits_len -= (no_of_bits * nCount);
              if (no_of_bits <= 8)
              {
                pui8 = pui8DATA(data, pDescr->offset);

                while (nCount > 0)
                {
                  *pui8 = vector->readField(readIndex,  no_of_bits);
                  LOG(INFO) << pDescr->sz << " = " << (unsigned)*pui8 << "\n";
                  pui8++;
                  bit_offset += no_of_bits;
                  nCount--;
                }
              }
              else if (no_of_bits <= 16)
              {
                pui16 = pui16DATA(data, pDescr->offset);

                while (nCount > 0)
                {
                 *pui16 = vector->readField(readIndex,  no_of_bits);
                  LOG(INFO) << pDescr->sz << " = " << *pui16 << "\n";
                  pui16++;
                  bit_offset += no_of_bits;
                  nCount--;
                }
              }
              else if (no_of_bits <= 32)
              { /* not supported */
                return ProcessError(readIndex,"csnStreamDecoder NOT IMPLEMENTED", 999, pDescr);
              }
              else
              {
                return ProcessError(readIndex,"csnStreamDecoder", CSN_ERROR_GENERAL, pDescr);
              }
            }
            else
            {
              return ProcessError(readIndex,"csnStreamDecoder", CSN_ERROR_NEED_MORE_BITS_TO_UNPACK, pDescr);
            }

            pDescr++;
            break;
          }

          case CSN_VARIABLE_TARRAY_OFFSET:
          case CSN_VARIABLE_TARRAY:
          case CSN_TYPE_ARRAY:
          {
            gint16      Status;
            csnStream_t arT    = *ar;
            guint16      nCount = (guint16) pDescr->i;
            guint16      nSize  = (guint16)(guint32)pDescr->serialize.value;

            pui8  = pui8DATA(data, pDescr->offset);

            if (CSN_VARIABLE_TARRAY == pDescr->type)
            { /* Count specified in field */
              nCount = *pui8DATA(data, pDescr->i);
            }
            else if (CSN_VARIABLE_TARRAY_OFFSET == pDescr->type)
            { /* Count specified in field */
              nCount = *pui8DATA(data, pDescr->i);
              nCount--; /* Offset 1 */
            }

            while (nCount--)    /* Changed to handle length = 0.  */
            {
              LOG(INFO) << pDescr->sz << "\n";
              csnStreamInit(&arT, bit_offset, remaining_bits_len);
              Status = csnStreamDecoder(&arT, (const CSN_DESCR*)pDescr->descr.ptr, vector, readIndex, pui8);
              if (Status >= 0)
              {
                pui8    += nSize;
                remaining_bits_len = arT.remaining_bits_len;
                bit_offset         = arT.bit_offset;
              }
              else
              {
                return Status;
              }
            }

            pDescr++;
            break;
          }

          case CSN_BITMAP:
          { /* bitmap with given length. The result is left aligned! */
            guint8 no_of_bits = (guint8) pDescr->i; /* length of bitmap */

            if (no_of_bits > 0)
            {

              if (no_of_bits <= 32)
              {
                guint32 ui32 = vector->readField(readIndex, no_of_bits);
                pui32       = pui32DATA(data, pDescr->offset);
                *pui32      = ui32;
                LOG(INFO) << pDescr->sz << " = " << *pui32 << "\n";
              }
              else if (no_of_bits <= 64)
              {
                guint64 ui64 = vector->readField(readIndex, no_of_bits);
                pui64       = pui64DATA(data, pDescr->offset);
                *pui64      = ui64;
                LOG(INFO) << pDescr->sz << " = " << *pui64 << "\n";
              }
              else
              {
              	return ProcessError(readIndex,"csnStreamDecoder NOT IMPLEMENTED", 999, pDescr);
              }

              remaining_bits_len -= no_of_bits;
              assert(remaining_bits_len >= 0);
              bit_offset += no_of_bits;
            }
            /* bitmap was successfully extracted or it was empty */

            pDescr++;
            break;
          }

          case CSN_TYPE:
          {
            gint16      Status;
            csnStream_t arT = *ar;
            LOG(INFO) << ": " << pDescr->sz << "\n";
            csnStreamInit(&arT, bit_offset, remaining_bits_len);
            Status = csnStreamDecoder(&arT, (const CSN_DESCR*)pDescr->descr.ptr, vector, readIndex, pvDATA(data, pDescr->offset));
            LOG(INFO) << ": End " << pDescr->sz << "\n";
            if (Status >= 0)
            {
              remaining_bits_len = arT.remaining_bits_len;
              bit_offset         = arT.bit_offset;
              pDescr++;
            }
            else
            { /* return error code Has already been processed:  */
              return Status;
            }

            break;
          }

          default:
          { /* descriptions of union elements other than above are illegal */
            return ProcessError(readIndex,"csnStreamDecoder", CSN_ERROR_IN_SCRIPT, pDescr);
          }
        }

        pDescr = pDescrNext;
        break;
      }

      case CSN_EXIST:
      case CSN_EXIST_LH:
      {
        guint8 fExist;

        pui8  = pui8DATA(data, pDescr->offset);

        if (CSN_EXIST_LH == pDescr->type)
        {
          fExist = get_masked_bits8(vector, readIndex, bit_offset, 1);
        }
        else
        {
          fExist = vector->readField(readIndex, 1);
        }

        *pui8 = fExist;
        LOG(INFO) << pDescr->sz << " = " << (unsigned)*pui8 <<"\n";
        pDescr++;
        remaining_bits_len -= 1;

        if (!fExist)
        {
          ar->remaining_bits_len  = remaining_bits_len;
          ar->bit_offset          = bit_offset;
          return remaining_bits_len;
        }

        break;
      }

      case CSN_NEXT_EXIST:
      {
        guint8 fExist;

        pui8  = pui8DATA(data, pDescr->offset);

        /* this if-statement represents the M_NEXT_EXIST_OR_NULL description element */
        if ((pDescr->descr.ptr != NULL) && (remaining_bits_len == 0))
        { /* no more bits to decode is fine here - end of message detected and allowed */

          /* Skip i entries + this entry */
          pDescr += pDescr->i + 1;

          /* pDescr now must be pointing to a CSN_END entry, if not this is an error */
          if ( pDescr->type != CSN_END )
          { /* Substract one more bit from remaining_bits_len to make the "not enough bits" error to be triggered */
            remaining_bits_len--;
          }

          /* Set the data member to "not exist" */
          *pui8 = 0;
          break;
        }

        /* the "regular" M_NEXT_EXIST description element */

        fExist = 0x00;
        if (vector->readField(readIndex, 1))
        {
          fExist = 0x01;
        }

        *pui8     = fExist;
        remaining_bits_len -= 1;
        LOG(INFO) << pDescr->sz << " = " << (unsigned)*pui8 << "\n" ;
        ++bit_offset;

        if (fExist == 0)
        { /* Skip 'i' entries */
          pDescr += pDescr->i;
        }

        pDescr++;
        break;
      }

      case CSN_NEXT_EXIST_LH:
      {
        guint8 fExist;
        pui8  = pui8DATA(data, pDescr->offset);

        /* this if-statement represents the M_NEXT_EXIST_OR_NULL_LH description element */
        if ((pDescr->descr.ptr != NULL) && (remaining_bits_len == 0))
        { /* no more bits to decode is fine here - end of message detected and allowed */

          /* skip 'i' entries + this entry */
          pDescr += pDescr->i + 1;

          /* pDescr now must be pointing to a CSN_END entry, if not this is an error */
          if ( pDescr->type != CSN_END )
          { /* substract one more bit from remaining_bits_len to make the "not enough bits" error to be triggered */
            remaining_bits_len--;
          }

          /* set the data member to "not exist" */
          *pui8 = 0;
          break;
        }

        /* the "regular" M_NEXT_EXIST_LH description element */
        fExist = get_masked_bits8(vector,readIndex,bit_offset, 1);
        LOG(INFO) << pDescr->sz << " = " << (unsigned)fExist << "\n" ;
        *pui8++   = fExist;
        remaining_bits_len -= 1;

        bit_offset++;

        if (fExist == 0)
        { /* Skip 'i' entries */
          pDescr += pDescr->i;
        }
        pDescr++;

        break;
      }

      case CSN_VARIABLE_BITMAP_1:
      { /* Bitmap from here and to the end of message */

        *pui8DATA(data, (gint16)pDescr->descr.value) = (guint8) remaining_bits_len; /* length of bitmap == remaining bits */

        /*no break -
         * with a length set we have a regular variable length bitmap so we continue */
      }

      case CSN_VARIABLE_BITMAP:
      { /* {CSN_VARIABLE_BITMAP, 0, offsetof(_STRUCT, _ElementCountField), offsetof(_STRUCT, _MEMBER), #_MEMBER}
         * <N: bit (5)> <bitmap: bit(N + offset)>
         * Bit array with length (in bits) specified in parameter (pDescr->descr)
         * The result is right aligned!
         */
        gint16 no_of_bits = *pui8DATA(data, (gint16)pDescr->descr.value);

        no_of_bits += pDescr->i; /* adjusted by offset */

        if (no_of_bits > 0)
        {
          remaining_bits_len -= no_of_bits;

          if (remaining_bits_len < 0)
          {
            return ProcessError(readIndex,"csnStreamDecoder", CSN_ERROR_NEED_MORE_BITS_TO_UNPACK, pDescr);
          }

          { /* extract bits */
            guint8* pui8 = pui8DATA(data, pDescr->offset);
            gint16 nB1  = no_of_bits & 0x07;/* no_of_bits Mod 8 */

            if (nB1 > 0)
            { /* take care of the first byte - it will be right aligned */
              *pui8 = vector->readField(readIndex, nB1);
              LOG(INFO) << pDescr->sz << " = " << (unsigned)*pui8 << "\n" ;
              pui8++;
              no_of_bits  -= nB1;
              bit_offset += nB1; /* (nB1 is no_of_bits Mod 8) */
            }

            /* remaining no_of_bits is a multiple of 8 or 0 */
            while (no_of_bits > 0)
            {
              *pui8 = vector->readField(readIndex, 8);
              LOG(INFO) << pDescr->sz << " = " << (unsigned)*pui8 << "\n" ;
              pui8++;
              no_of_bits -= 8;
            }
          }
        }
        pDescr++;
        break;
      }

      case CSN_LEFT_ALIGNED_VAR_BMP_1:
      { /* Bitmap from here and to the end of message */

        *pui8DATA(data, (gint16)pDescr->descr.value) = (guint8) remaining_bits_len; /* length of bitmap == remaining bits */

        /* no break -
         * with a length set we have a regular left aligned variable length bitmap so we continue
         */
      }

      case CSN_LEFT_ALIGNED_VAR_BMP:
      { /* {CSN_LEFT_ALIGNED_VAR_BMP, _OFFSET, (void*)offsetof(_STRUCT, _ElementCountField), offsetof(_STRUCT, _MEMBER), #_MEMBER}
         * <N: bit (5)> <bitmap: bit(N + offset)>
         * bit array with length (in bits) specified in parameter (pDescr->descr)
         */
        gint16 no_of_bits = *pui8DATA(data, (gint16)pDescr->descr.value);/* Size of bitmap */

        no_of_bits += pDescr->i;/* size adjusted by offset */

        if (no_of_bits > 0)
        {
          remaining_bits_len -= no_of_bits;

          if (remaining_bits_len < 0)
          {
            return ProcessError(readIndex,"csnStreamDecoder", CSN_ERROR_NEED_MORE_BITS_TO_UNPACK, pDescr);
          }

          { /* extract bits */
            guint8* pui8 = pui8DATA(data, pDescr->offset);
            gint16 nB1  = no_of_bits & 0x07;/* no_of_bits Mod 8 */

            while (no_of_bits > 0)
            {
              *pui8 = vector->readField(readIndex, 8);
              LOG(INFO) << pDescr->sz << " = " << (unsigned)*pui8 << "\n" ;
              pui8++;
              no_of_bits -= 8;
            }
            if (nB1 > 0)
            { 
              *pui8 = vector->readField(readIndex, nB1);
              LOG(INFO) << pDescr->sz << " = " << (unsigned)*pui8 << "\n" ;
              pui8++;
              no_of_bits  -= nB1;
              bit_offset += nB1; /* (nB1 is no_of_bits Mod 8) */
            }
          }
        }

        /* bitmap was successfully extracted or it was empty */
        pDescr++;
        break;
      }

      case CSN_VARIABLE_ARRAY:
      { /* {int type; int i; void* descr; int offset; const char* sz; } CSN_DESCR;
         * {CSN_VARIABLE_ARRAY, _OFFSET, (void*)offsetof(_STRUCT, _ElementCountField), offsetof(_STRUCT, _MEMBER), #_MEMBER}
         * Array with length specified in parameter:
         *  <count: bit (x)>
         *  <list: octet(count + offset)>
         */
        gint16 count = *pui8DATA(data, (gint16)pDescr->descr.value);

        count += pDescr->i; /* Adjusted by offset */

        if (count > 0)
        {
          remaining_bits_len -= count * 8;

          if (remaining_bits_len < 0)
          {
            return ProcessError(readIndex,"csnStreamDecoder", CSN_ERROR_NEED_MORE_BITS_TO_UNPACK, pDescr);
          }

          pui8 = pui8DATA(data, pDescr->offset);

          while (count > 0)
          {
            readIndex -= 8;
            *pui8 = vector->readField(readIndex, 8);
            LOG(INFO) << pDescr->sz << " = " << (unsigned)*pui8 << "\n";
            pui8++;
            bit_offset += 8;
            count--;
          }
        }

        pDescr++;
        break;
      }

      case CSN_RECURSIVE_ARRAY:
      { /* Recursive way to specify an array: <list> ::= {1 <number: bit (4)> <list> | 0}
         *  or more generally:                <list> ::= { <tag> <element> <list> | <EndTag> }
         *  where <element> ::= bit(value)
         *        <tag>     ::= 0 | 1
         *        <EndTag>  ::= reversed tag i.e. tag == 1 -> EndTag == 0 and vice versa
         * {CSN_RECURSIVE_ARRAY, _BITS, (void*)offsetof(_STRUCT, _ElementCountField), offsetof(_STRUCT, _MEMBER), #_MEMBER}
         * REMARK: recursive way to specify an array but an iterative implementation!
         */
        gint16 no_of_bits        = pDescr->i;
        guint8  ElementCount = 0;

        pui8  = pui8DATA(data, pDescr->offset);

        while (existNextElement(vector, readIndex, Tag))
        { /* tag control shows existence of next list elements */
          LOG(INFO) <<  pDescr->sz << " = Exist \n" ;
          bit_offset++;
          remaining_bits_len--;

          /* extract and store no_of_bits long element from bitstream */
          *pui8 = vector->readField(readIndex, no_of_bits);
          LOG(INFO) << pDescr->sz << " = " << (unsigned)*pui8 << "\n";
          pui8++;
          remaining_bits_len -= no_of_bits;
          ElementCount++;

          if (remaining_bits_len < 0)
          {
            return ProcessError(readIndex,"csnStreamDecoder", CSN_ERROR_NEED_MORE_BITS_TO_UNPACK, pDescr);
          }

          bit_offset += no_of_bits;
        }

        LOG(INFO) << pDescr->sz << " = " << vector->readField(readIndex, 1) << "\n";
        /* existNextElement() returned FALSE, 1 bit consumed */
        bit_offset++;

        /* Store the counted number of elements of the array */
        *pui8DATA(data, (gint16)pDescr->descr.value) = ElementCount;

        pDescr++;
        break;
      }

      case CSN_RECURSIVE_TARRAY:
      { /* Recursive way to specify an array of type: <lists> ::= { 1 <type> } ** 0 ;
         *  M_REC_TARRAY(_STRUCT, _MEMBER, _MEMBER_TYPE, _ElementCountField)
         * {t, offsetof(_STRUCT, _ElementCountField), (void*)CSNDESCR_##_MEMBER_TYPE, offsetof(_STRUCT, _MEMBER), #_MEMBER, (StreamSerializeFcn_t)sizeof(_MEMBER_TYPE)}
         */
        gint16 nSizeElement = (gint16)(gint32)pDescr->serialize.value;
        guint8  ElementCount = 0;
        pui8  = pui8DATA(data, pDescr->offset);

        while (existNextElement(vector, readIndex, Tag))
        { /* tag control shows existence of next list elements */
          LOG(INFO) <<  pDescr->sz << " = Exist \n" ;
          /* existNextElement() returned TRUE, 1 bit consumed */
          bit_offset++;
          remaining_bits_len--;
          ElementCount++;

          { /* unpack the following data structure */
            csnStream_t arT = *ar;
            gint16      Status;
            csnStreamInit(&arT, bit_offset, remaining_bits_len);
            Status = csnStreamDecoder(&arT, (const CSN_DESCR*)pDescr->descr.ptr, vector, readIndex, pvDATA(data, pDescr->offset));

            if (Status >= 0)
            { /* successful completion */
              pui8    += nSizeElement;  /* -> to next data element */
              remaining_bits_len = arT.remaining_bits_len;
              bit_offset         = arT.bit_offset;
            }
            else
            { /* something went awry */
              return Status;
            }
          }

          if (remaining_bits_len < 0)
          {
            return ProcessError(readIndex,"csnStreamDecoder", CSN_ERROR_NEED_MORE_BITS_TO_UNPACK, pDescr);
          }
        }

        LOG(INFO) << pDescr->sz << " = " << vector->readField(readIndex, 1) << "\n";

        /* existNextElement() returned FALSE, 1 bit consumed */
        bit_offset++;

        /* Store the counted number of elements of the array */
        *pui8DATA(data, (gint16)(gint32)pDescr->i) = ElementCount;

        pDescr++;
        break;
      }

      case CSN_RECURSIVE_TARRAY_2:
      { /* Recursive way to specify an array of type: <list> ::= <type> { 0 <type> } ** 1 ; */

        Tag = REVERSED_TAG;

        /* NO break -
         * handling is exactly the same as for CSN_RECURSIVE_TARRAY_1 so we continue
         */
      }

      case CSN_RECURSIVE_TARRAY_1:
      { /* Recursive way to specify an array of type: <lists> ::= <type> { 1 <type> } ** 0 ;
         * M_REC_TARRAY(_STRUCT, _MEMBER, _MEMBER_TYPE, _ElementCountField)
         * {t, offsetof(_STRUCT, _ElementCountField), (void*)CSNDESCR_##_MEMBER_TYPE, offsetof(_STRUCT, _MEMBER), #_MEMBER, (StreamSerializeFcn_t)sizeof(_MEMBER_TYPE)}
         */
        gint16      nSizeElement = (gint16)(gint32)pDescr->serialize.value;
        guint8       ElementCount = 0;
        csnStream_t arT          = *ar;
        gboolean     EndOfList    = FALSE;
        gint16      Status;
        pui8  = pui8DATA(data, pDescr->offset);

        do
        { /* get data element */
          ElementCount++;

          LOG(INFO) << pDescr->sz << " { \n"; 
          csnStreamInit(&arT, bit_offset, remaining_bits_len);
          Status = csnStreamDecoder(&arT, (const CSN_DESCR*)pDescr->descr.ptr, vector, readIndex, pvDATA(data, pDescr->offset));

          if (Status >= 0)
          { /* successful completion */
            pui8    += nSizeElement;  /* -> to next */
            remaining_bits_len = arT.remaining_bits_len;
            bit_offset         = arT.bit_offset;
          }
          else
          { /* something went awry */
            return Status;
          }

          if (remaining_bits_len < 0)
          {
            return ProcessError(readIndex,"csnStreamDecoder", CSN_ERROR_NEED_MORE_BITS_TO_UNPACK, pDescr);
          }

          /* control of next element's tag */
          LOG(INFO) << pDescr->sz << " } \n"; 
          EndOfList         = !(existNextElement(vector, readIndex, Tag));

          bit_offset++;
          remaining_bits_len--; /* 1 bit consumed (tag) */
        } while (!EndOfList);


        /* Store the count of the array */
        *pui8DATA(data, pDescr->i) = ElementCount;
        Tag = STANDARD_TAG; /* in case it was set to "reversed" */
        pDescr++;
        break;
      }

      case CSN_FIXED:
      { /* Verify the fixed bits */
        guint8  no_of_bits = (guint8) pDescr->i;
        guint32 ui32;

        if (no_of_bits <= 32)
        {
          ui32 = vector->readField(readIndex, no_of_bits);
        }
        else
        {
          return ProcessError(readIndex,"no_of_bits > 32", -1, pDescr);
        }
        if (ui32 != (unsigned)(gint32)pDescr->offset)
        {
          return ProcessError(readIndex,"csnStreamDecoder FIXED value does not match", -1, pDescr);
        }

        LOG(INFO) << pDescr->sz<< " = " << ui32 << "\n"; 
        remaining_bits_len   -= no_of_bits;
        bit_offset += no_of_bits;
        pDescr++;
        break;
      }

      case CSN_CALLBACK:
      {
        return ProcessError(readIndex,"csnStreamDecoder Callback not implemented", -1, pDescr);
        break;
      }

      case CSN_TRAP_ERROR:
      {
        return ProcessError(readIndex,"csnStreamDecoder", pDescr->i, pDescr);
      }

      case CSN_END:
      {
        ar->remaining_bits_len  = remaining_bits_len;
        ar->bit_offset = bit_offset;
        return remaining_bits_len;
      }

      default:
      {
        assert(0);
      }


    }

  } while (remaining_bits_len >= 0);

  return ProcessError(readIndex,"csnStreamDecoder", CSN_ERROR_NEED_MORE_BITS_TO_UNPACK, pDescr);
}




gint16 csnStreamEncoder(csnStream_t* ar, const CSN_DESCR* pDescr, BitVector *vector, size_t& writeIndex, void* data)
{
  gint  remaining_bits_len = ar->remaining_bits_len;
  gint  bit_offset         = ar->bit_offset;
  guint8*  pui8;
  guint16* pui16;
  guint32* pui32;
  guint64* pui64;

  guint8 Tag = STANDARD_TAG;

  if (remaining_bits_len <= 0)
  {
    return 0;
  }

  do
  {
    switch (pDescr->type)
    {
      case CSN_BIT:
      {
        if (remaining_bits_len > 0)
        {
          pui8  = pui8DATA(data, pDescr->offset);
          vector->writeField(writeIndex, *pui8, 1);
          LOG(INFO) << pDescr->sz << " = " << (unsigned)*pui8 << "\n";
          /* end add the bit value to protocol tree */
        }
        else
        {
          return ProcessError(writeIndex,"csnStreamEncoder", CSN_ERROR_NEED_MORE_BITS_TO_UNPACK, pDescr);
        }

        pDescr++;
        remaining_bits_len--;
        bit_offset++;
        break;
      }

      case CSN_NULL:
      { /* Empty member! */
        pDescr++;
        break;
      }

      case CSN_UINT:
      {
        guint8 no_of_bits = (guint8) pDescr->i;

        if (remaining_bits_len >= no_of_bits)
        {
          if (no_of_bits <= 8)
          {
            pui8      = pui8DATA(data, pDescr->offset);
            vector->writeField(writeIndex, *pui8, no_of_bits);
            LOG(INFO) << pDescr->sz << " = " << (unsigned)*pui8 << "\n";
          }
          else if (no_of_bits <= 16)
          {
            pui16       = pui16DATA(data, pDescr->offset);
            vector->writeField(writeIndex, *pui16, no_of_bits);
            LOG(INFO) << pDescr->sz << " = " << *pui16 << "\n";
          }
          else if (no_of_bits <= 32)
          {
            pui32       = pui32DATA(data, pDescr->offset);
            vector->writeField(writeIndex, *pui32, no_of_bits);
            LOG(INFO) << pDescr->sz << " = " << *pui32 << "\n";
          }
          else
          {
            return ProcessError(writeIndex,"csnStreamEncoder", CSN_ERROR_GENERAL, pDescr);
          }
        }
        else
        {
          return ProcessError(writeIndex,"csnStreamEncoder", CSN_ERROR_NEED_MORE_BITS_TO_UNPACK, pDescr);
        }

        remaining_bits_len -= no_of_bits;
        bit_offset += no_of_bits;
        pDescr++;
        break;
      }

      case CSN_UINT_OFFSET:
      {
        guint8 no_of_bits = (guint8) pDescr->i;

        if (remaining_bits_len >= no_of_bits)
        {
          if (no_of_bits <= 8)
          {
            pui8      = pui8DATA(data, pDescr->offset);
            vector->writeField(writeIndex, *pui8 - (guint8)pDescr->descr.value, no_of_bits);
            LOG(INFO) << pDescr->sz << " = " << (unsigned)(*pui8 - (guint8)pDescr->descr.value) << "\n";
          }
          else if (no_of_bits <= 16)
          {
            pui16       = pui16DATA(data, pDescr->offset);
            vector->writeField(writeIndex, *pui16 - (guint16)pDescr->descr.value, no_of_bits);
            LOG(INFO) << pDescr->sz << " = " << *pui16 - (guint16)pDescr->descr.value << "\n";
          }
          else if (no_of_bits <= 32)
          {
            pui32       = pui32DATA(data, pDescr->offset);
            vector->writeField(writeIndex, *pui32 - (guint16)pDescr->descr.value, no_of_bits);
            LOG(INFO) << pDescr->sz << " = " << *pui32 - (guint16)pDescr->descr.value << "\n";
          }
          else
          {
            return ProcessError(writeIndex,"csnStreamEncoder", CSN_ERROR_GENERAL, pDescr);
          }
        }
        else
        {
          return ProcessError(writeIndex,"csnStreamEncoder", CSN_ERROR_NEED_MORE_BITS_TO_UNPACK, pDescr);
        }

        remaining_bits_len -= no_of_bits;
        bit_offset += no_of_bits;
        pDescr++;
        break;
      }

      case CSN_UINT_LH:
      {
        guint8 no_of_bits = (guint8) pDescr->i;

        if (remaining_bits_len >= no_of_bits)
        {
          remaining_bits_len -= no_of_bits;
          if (no_of_bits <= 8)
          {
            pui8      = pui8DATA(data, pDescr->offset);
            vector->writeField(writeIndex, *pui8, no_of_bits);
            // TODO : Change get_masked_bits8()
            writeIndex -= no_of_bits;
            guint8 ui8 = get_masked_bits8(vector, writeIndex, bit_offset, no_of_bits);
            writeIndex -= no_of_bits;
            vector->writeField(writeIndex, ui8, no_of_bits);
            LOG(INFO) << pDescr->sz << " = " << (unsigned)*pui8 << "\n";

          }
          else
          {/* Maybe we should support more than 8 bits ? */
            return ProcessError(writeIndex,"csnStreamEncoder", CSN_ERROR_GENERAL, pDescr);
          }
        }
        else
        {
          return ProcessError(writeIndex,"csnStreamEncoder", CSN_ERROR_NEED_MORE_BITS_TO_UNPACK, pDescr);
        }

        remaining_bits_len -= no_of_bits;
        bit_offset += no_of_bits;
        pDescr++;
        break;
      }

      case CSN_UINT_ARRAY:
      {
        guint8  no_of_bits  = (guint8) pDescr->i;
        guint16 nCount = (guint16)pDescr->descr.value; /* nCount supplied by value i.e. M_UINT_ARRAY(...) */

        if (pDescr->serialize.value != 0)
        { /* nCount specified by a reference to field holding value i.e. M_VAR_UINT_ARRAY(...) */
          nCount = *pui16DATA(data, nCount);
        }

        if (remaining_bits_len >= no_of_bits)
        {
          remaining_bits_len -= (no_of_bits*nCount);
          if (no_of_bits <= 8)
          {
            pui8 = pui8DATA(data, pDescr->offset);
            do
            {
	      vector->writeField(writeIndex, *pui8, no_of_bits);
              LOG(INFO) << pDescr->sz << " = " << (unsigned)*pui8 << "\n";
              pui8++;
              bit_offset += no_of_bits;
            } while (--nCount > 0);
          }
          else if (no_of_bits <= 16)
          {
            return ProcessError(writeIndex,"csnStreamEncoder NOTIMPLEMENTED", 999, pDescr);
          }
          else if (no_of_bits <= 32)
          {
            return ProcessError(writeIndex,"csnStreamEncoder NOTIMPLEMENTED", 999, pDescr);
          }
          else
          {
            return ProcessError(writeIndex,"csnStreamEncoder", CSN_ERROR_GENERAL, pDescr);
          }
        }
        else
        {
          return ProcessError(writeIndex,"csnStreamEncoder", CSN_ERROR_NEED_MORE_BITS_TO_UNPACK, pDescr);
        }
        pDescr++;
        break;
      }

      case CSN_VARIABLE_TARRAY_OFFSET:
      case CSN_VARIABLE_TARRAY:
      case CSN_TYPE_ARRAY:
      {
        gint16      Status;
        csnStream_t arT    = *ar;
        gint16      nCount = pDescr->i;
        guint16     nSize  = (guint16)(gint32)pDescr->serialize.value;

        pui8 = pui8DATA(data, pDescr->offset);
        if (pDescr->type == CSN_VARIABLE_TARRAY)
        { /* Count specified in field */
          nCount = *pui8DATA(data, pDescr->i);
        }
        else if (pDescr->type == CSN_VARIABLE_TARRAY_OFFSET)
        { /* Count specified in field */
          nCount = *pui8DATA(data, pDescr->i);
	  /*  nCount--; the 1 offset is already taken into account in CSN_UINT_OFFSET */
        }

        while (nCount > 0)
        { /* resulting array of length 0 is possible
           * but no bits shall be read from bitstream
           */

          LOG(INFO) << pDescr->sz << " : \n";
          csnStreamInit(&arT, bit_offset, remaining_bits_len);
          Status = csnStreamEncoder(&arT, (const CSN_DESCR*)pDescr->descr.ptr, vector, writeIndex, pui8);
          if (Status >= 0)
          {
            pui8    += nSize;
            remaining_bits_len = arT.remaining_bits_len;
            bit_offset         = arT.bit_offset;

          }
          else
          {
            return Status;
          }
          nCount--;
        }

        pDescr++;
        break;
      }

      case CSN_BITMAP:
      { /* bitmap with given length. The result is left aligned! */
        guint8 no_of_bits = (guint8) pDescr->i; /* length of bitmap */

        if (no_of_bits > 0)
        {

          if (no_of_bits <= 32)
          {
            pui32 = pui32DATA(data, pDescr->offset);
            vector->writeField(writeIndex, *pui32, no_of_bits);
	    LOG(INFO) << pDescr->sz << " = " << *pui32 << "\n";
          }
          else if (no_of_bits <= 64)
          {
            pui64 = pui64DATA(data, pDescr->offset);
            vector->writeField(writeIndex, *pui64, no_of_bits);
      	    LOG(INFO) << pDescr->sz << " = " << *pui64 << "\n";
          }
          else
          {
          	return ProcessError(writeIndex,"csnStreamEncoder NOT IMPLEMENTED", 999, pDescr);
          }

          remaining_bits_len -= no_of_bits;
          assert(remaining_bits_len >= 0);
          bit_offset += no_of_bits;
        }
        /* bitmap was successfully extracted or it was empty */

        pDescr++;
        break;
      }

      case CSN_TYPE:
      {
        gint16      Status;
        csnStream_t arT = *ar;
        LOG(INFO) << ": " << pDescr->sz << "\n";
        csnStreamInit(&arT, bit_offset, remaining_bits_len);
        Status = csnStreamEncoder(&arT, (const CSN_DESCR*)pDescr->descr.ptr, vector, writeIndex, pvDATA(data, pDescr->offset));
        LOG(INFO) << ": End " << pDescr->sz << "\n";
        if (Status >= 0)
        {

          remaining_bits_len  = arT.remaining_bits_len;
          bit_offset          = arT.bit_offset;
          pDescr++;
        }
        else
        {
          /* Has already been processed: ProcessError("csnStreamEncoder", Status, pDescr);  */
          return Status;
        }

        break;
      }

      case CSN_CHOICE:
      {
        //gint16 count = pDescr->i;
        guint8 i     = 0;
        CSN_ChoiceElement_t* pChoice = (CSN_ChoiceElement_t*) pDescr->descr.ptr;

        pui8          = pui8DATA(data, pDescr->offset);
        i = *pui8;
        pChoice += i;
        guint8 no_of_bits = pChoice->bits;
        guint8 value = pChoice->value;
        LOG(INFO) << pChoice->descr.sz << " = " << (unsigned)value << "\n";
        vector->writeField(writeIndex, value, no_of_bits);

        CSN_DESCR   descr[2];
        gint16      Status;
        csnStream_t arT = *ar;

        descr[0]      = pChoice->descr;
        memset(&descr[1], 0x00, sizeof(CSN_DESCR));
        descr[1].type = CSN_END;
        bit_offset += no_of_bits;
        remaining_bits_len -= no_of_bits;

        csnStreamInit(&arT, bit_offset, remaining_bits_len);
        Status = csnStreamEncoder(&arT, descr, vector, writeIndex, data);

        if (Status >= 0)
        {
          remaining_bits_len = arT.remaining_bits_len;
          bit_offset         = arT.bit_offset;
        }
        else
        {
          return Status;
        }

        pDescr++;
        break;
      }

   case CSN_SERIALIZE:
      {
        StreamSerializeFcn_t serialize = pDescr->serialize.fcn;
        csnStream_t          arT       = *ar;
        gint16               Status = -1;
        size_t lengthIndex;

        // store writeIndex for length value (7 bit)
	lengthIndex = writeIndex;
        writeIndex += 7;
        bit_offset += 7;
        remaining_bits_len -= 7;
        arT.direction = 0;
        csnStreamInit(&arT, bit_offset, remaining_bits_len);
        Status = serialize(&arT, vector, writeIndex, pvDATA(data, pDescr->offset));
	
        vector->writeField(lengthIndex, writeIndex-lengthIndex-7, 7);
        LOG(INFO) << pDescr->sz << " length = " << writeIndex-lengthIndex << "\n";

        if (Status >= 0)
        {
          remaining_bits_len = arT.remaining_bits_len;
          bit_offset         = arT.bit_offset;
          pDescr++;
        }
        else
        {
          // Has already been processed: 
          return Status;
        }

        break;
      }

      case CSN_UNION_LH:
      case CSN_UNION:
      {
        gint16           Bits;
        guint8           index;
        gint16           count      = pDescr->i;
        const CSN_DESCR* pDescrNext = pDescr;

        pDescrNext += count + 1; /* now this is next after the union */
        if ((count <= 0) || (count > 16))
        {
          return ProcessError(writeIndex,"csnStreamEncoder", CSN_ERROR_INVALID_UNION_INDEX, pDescr);
        }

        /* Now get the bits to extract the index */
        Bits = ixBitsTab[count];
        index = 0;

        /* Assign UnionType */
        pui8  = pui8DATA(data, pDescr->offset);
	//read index from data and write to vector
        vector->writeField(writeIndex, *pui8, Bits);

	//decode index 
        writeIndex -= Bits;
        
        while (Bits > 0)
        {
          index <<= 1;

          if (CSN_UNION_LH == pDescr->type)
          {
            index |= get_masked_bits8(vector,writeIndex, bit_offset, 1);
          }
          else
          {
            index |= vector->readField(writeIndex, 1);
          }

          remaining_bits_len--;
          bit_offset++;
          Bits--;
        }

        writeIndex -= Bits;
        vector->writeField(writeIndex, index, Bits);


        /* script index to continue on, limited in case we do not have a power of 2 */
        pDescr += (MIN(index + 1, count));
        LOG(INFO) << pDescr->sz << " = " << (unsigned)index  << "\n";

        switch (pDescr->type)
        { /* get the right element of the union based on computed index */

          case CSN_BIT:
          {
            pui8  = pui8DATA(data, pDescr->offset);
            vector->writeField(writeIndex, *pui8, 1);
            LOG(INFO) << pDescr->sz << " = " << (unsigned)*pui8 << "\n";
            remaining_bits_len -= 1;
            bit_offset++;
            pDescr++;
            break;
          }

          case CSN_NULL:
          { /* Empty member! */
            pDescr++;
            break;
          }

          case CSN_UINT:
          {
            guint8 no_of_bits = (guint8) pDescr->i;
            if (remaining_bits_len >= no_of_bits)
            {
              remaining_bits_len -= no_of_bits;

              if (no_of_bits <= 8)
              {
                pui8      = pui8DATA(data, pDescr->offset);
                vector->writeField(writeIndex, *pui8, no_of_bits);
                LOG(INFO) << pDescr->sz << " = " << (unsigned)*pui8 << "\n";
              }
              else if (no_of_bits <= 16)
              {
                pui16       = pui16DATA(data, pDescr->offset);
                vector->writeField(writeIndex, *pui16, no_of_bits);
                LOG(INFO) << pDescr->sz << " = " << *pui16 << "\n";
              }
              else if (no_of_bits <= 32)
              {
                pui32       = pui32DATA(data, pDescr->offset);
                vector->writeField(writeIndex, *pui32, no_of_bits);
                LOG(INFO) << pDescr->sz << " = " << *pui32 << "\n";
              }
              else
              {
                return ProcessError(writeIndex,"csnStreamEncoder", CSN_ERROR_GENERAL, pDescr);
              }
            }
            else
            {
              return ProcessError(writeIndex,"csnStreamEncoder", CSN_ERROR_GENERAL, pDescr);
            }

            bit_offset += no_of_bits;
            pDescr++;
            break;
          }

          case CSN_UINT_OFFSET:
          {
            guint8 no_of_bits = (guint8) pDescr->i;

            if (remaining_bits_len >= no_of_bits)
            {
              if (no_of_bits <= 8)
              {
                pui8      = pui8DATA(data, pDescr->offset);
                vector->writeField(writeIndex, *pui8 - (guint8)pDescr->descr.value, no_of_bits);
                LOG(INFO) << pDescr->sz << " = " << (unsigned)(*pui8 - (guint8)pDescr->descr.value) << "\n";
              }
              else if (no_of_bits <= 16)
              {
                pui16       = pui16DATA(data, pDescr->offset);
                vector->writeField(writeIndex, *pui16 - (guint16)pDescr->descr.value, no_of_bits);
                LOG(INFO) << pDescr->sz << " = " << *pui16 - (guint16)pDescr->descr.value << "\n";
              }
              else if (no_of_bits <= 32)
              {
                pui32       = pui32DATA(data, pDescr->offset);
                vector->writeField(writeIndex, *pui32 - (guint16)pDescr->descr.value, no_of_bits);
                LOG(INFO) << pDescr->sz << " = " << *pui32 - (guint16)pDescr->descr.value << "\n";
              }
              else
              {
                return ProcessError(writeIndex,"csnStreamEncoder", CSN_ERROR_GENERAL, pDescr);
              }
            }
            else
            {
              return ProcessError(writeIndex,"csnStreamEncoder", CSN_ERROR_NEED_MORE_BITS_TO_UNPACK, pDescr);
            }

            remaining_bits_len -= no_of_bits;
            bit_offset += no_of_bits;
            pDescr++;
            break;
          }

          case CSN_UINT_LH:
          {
            guint8 no_of_bits = (guint8) pDescr->i;

            if (remaining_bits_len >= no_of_bits)
            {
              remaining_bits_len -= no_of_bits;
              if (no_of_bits <= 8)
              {
                pui8      = pui8DATA(data, pDescr->offset);
                vector->writeField(writeIndex, *pui8, no_of_bits);
                // TODO : Change get_masked_bits8()
                writeIndex -= no_of_bits;
                guint8 ui8 = get_masked_bits8(vector, writeIndex, bit_offset, no_of_bits);
                writeIndex -= no_of_bits;
                vector->writeField(writeIndex, ui8, no_of_bits);
                LOG(INFO) << pDescr->sz << " = " << (unsigned)*pui8 << "\n";

              }
              else
              {/* Maybe we should support more than 8 bits ? */
                return ProcessError(writeIndex,"csnStreamEncoder", CSN_ERROR_GENERAL, pDescr);
              }
            }
            else
            {
              return ProcessError(writeIndex,"csnStreamEncoder", CSN_ERROR_NEED_MORE_BITS_TO_UNPACK, pDescr);
            }

            remaining_bits_len -= no_of_bits;
            bit_offset += no_of_bits;
            pDescr++;
            break;
          }

          case CSN_UINT_ARRAY:
          {
            guint8  no_of_bits  = (guint8) pDescr->i;
            guint16 nCount = (guint16)pDescr->descr.value; /* nCount supplied by value i.e. M_UINT_ARRAY(...) */

            if (pDescr->serialize.value != 0)
            { /* nCount specified by a reference to field holding value i.e. M_VAR_UINT_ARRAY(...) */
              nCount = *pui16DATA(data, nCount);
            }

            if (remaining_bits_len >= no_of_bits)
            {
              remaining_bits_len -= (no_of_bits*nCount);
              if (no_of_bits <= 8)
              {
                pui8 = pui8DATA(data, pDescr->offset);
                do
                {
	          vector->writeField(writeIndex, *pui8, no_of_bits);
                  LOG(INFO) << pDescr->sz << " = " << (unsigned)*pui8 << "\n";
                  pui8++;
                  bit_offset += no_of_bits;
                } while (--nCount > 0);
              }
              else if (no_of_bits <= 16)
              {
                return ProcessError(writeIndex,"csnStreamEncoder NOTIMPLEMENTED", 999, pDescr);
              }
              else if (no_of_bits <= 32)
              {
                return ProcessError(writeIndex,"csnStreamEncoder NOTIMPLEMENTED", 999, pDescr);
              }
              else
              {
                return ProcessError(writeIndex,"csnStreamEncoder", CSN_ERROR_GENERAL, pDescr);
              }
            }
            else
            {
              return ProcessError(writeIndex,"csnStreamEncoder", CSN_ERROR_NEED_MORE_BITS_TO_UNPACK, pDescr);
            }
            pDescr++;
            break;
          }

          case CSN_VARIABLE_TARRAY_OFFSET:
          case CSN_VARIABLE_TARRAY:
          case CSN_TYPE_ARRAY:
          {
            gint16      Status;
            csnStream_t arT    = *ar;
            gint16      nCount = pDescr->i;
            guint16     nSize  = (guint16)(gint32)pDescr->serialize.value;

            pui8 = pui8DATA(data, pDescr->offset);
            if (pDescr->type == CSN_VARIABLE_TARRAY)
            { /* Count specified in field */
              nCount = *pui8DATA(data, pDescr->i);
            }
            else if (pDescr->type == CSN_VARIABLE_TARRAY_OFFSET)
            { /* Count specified in field */
              nCount = *pui8DATA(data, pDescr->i);
              /*  nCount--; the 1 offset is already taken into account in CSN_UINT_OFFSET */
            }

            while (nCount > 0)
            { /* resulting array of length 0 is possible
               * but no bits shall be read from bitstream
               */

              LOG(INFO) << pDescr->sz << " : \n";
              csnStreamInit(&arT, bit_offset, remaining_bits_len);
              Status = csnStreamEncoder(&arT, (const CSN_DESCR*)pDescr->descr.ptr, vector, writeIndex, pui8);
              if (Status >= 0)
              {
                pui8    += nSize;
                remaining_bits_len = arT.remaining_bits_len;
                bit_offset         = arT.bit_offset;
              }
              else
              {
                return Status;
              }
              nCount--;
            }

            pDescr++;
            break;
          }

          case CSN_BITMAP:
          { /* bitmap with given length. The result is left aligned! */
            guint8 no_of_bits = (guint8) pDescr->i; /* length of bitmap */

            if (no_of_bits > 0)
            {

              if (no_of_bits <= 32)
              {
                pui32 = pui32DATA(data, pDescr->offset);
                vector->writeField(writeIndex, *pui32, no_of_bits);
                LOG(INFO) << pDescr->sz << " = " << *pui32 << "\n";
              }
              else if (no_of_bits <= 64)
              {
                pui64 = pui64DATA(data, pDescr->offset);
                vector->writeField(writeIndex, *pui64, no_of_bits);
                LOG(INFO) << pDescr->sz << " = " << *pui64 << "\n";
              }
              else
              {
              	return ProcessError(writeIndex,"csnStreamEncoder NOT IMPLEMENTED", 999, pDescr);
              }

              remaining_bits_len -= no_of_bits;
              assert(remaining_bits_len >= 0);
              bit_offset += no_of_bits;
            }
            /* bitmap was successfully extracted or it was empty */

            pDescr++;
            break;
          }

          case CSN_TYPE:
          {
            gint16      Status;
            csnStream_t arT = *ar;
            LOG(INFO) << ": " << pDescr->sz << "\n";
            csnStreamInit(&arT, bit_offset, remaining_bits_len);
            Status = csnStreamEncoder(&arT, (const CSN_DESCR*)pDescr->descr.ptr, vector, writeIndex, pvDATA(data, pDescr->offset));
            LOG(INFO) << ": End " << pDescr->sz << "\n";
            if (Status >= 0)
            {
              remaining_bits_len  = arT.remaining_bits_len;
              bit_offset          = arT.bit_offset;
              pDescr++;
            }
            else
            {
              /* Has already been processed: ProcessError("csnStreamEncoder", Status, pDescr);  */
              return Status;
            }

            break;
          }

          default:
          { /* descriptions of union elements other than above are illegal */
            return ProcessError(writeIndex,"csnStreamEncoder", CSN_ERROR_IN_SCRIPT, pDescr);
          }
        }

        pDescr = pDescrNext;
        break;
      }

      case CSN_EXIST:
      case CSN_EXIST_LH:
      {
        guint8 fExist;
        unsigned exist = 0;
        pui8  = pui8DATA(data, pDescr->offset);
        exist = *pui8;
        vector->writeField(writeIndex, *pui8, 1);
        writeIndex--;
        if (CSN_EXIST_LH == pDescr->type)
        {
          fExist = get_masked_bits8(vector, writeIndex, bit_offset, 1);
        }
        else
        {
          fExist = vector->readField(writeIndex, 1);
        }
        writeIndex--;
        vector->writeField(writeIndex, fExist, 1);
        LOG(INFO) << pDescr->sz << " = " << (unsigned)fExist << "\n";
        pDescr++;
        remaining_bits_len -= 1;

        if (!exist)
        {
          ar->remaining_bits_len  = remaining_bits_len;
          ar->bit_offset          = bit_offset;
          return remaining_bits_len;
        }
        break;
      }

      case CSN_NEXT_EXIST:
      {
        guint8 fExist;

        pui8  = pui8DATA(data, pDescr->offset);

        /* this if-statement represents the M_NEXT_EXIST_OR_NULL description element */
        if ((pDescr->descr.ptr != NULL) && (remaining_bits_len == 0))
        { /* no more bits to decode is fine here - end of message detected and allowed */

          /* Skip i entries + this entry */
          pDescr += pDescr->i + 1;

          /* pDescr now must be pointing to a CSN_END entry, if not this is an error */
          if ( pDescr->type != CSN_END )
          { /* Substract one more bit from remaining_bits_len to make the "not enough bits" error to be triggered */
            remaining_bits_len--;
          }

          break;
        }

        vector->writeField(writeIndex, *pui8, 1);
        fExist = *pui8;
        LOG(INFO) << pDescr->sz << " = " << (unsigned)*pui8 << "\n";
        remaining_bits_len -= 1;

        ++bit_offset;

        if (fExist == 0)
        { /* Skip 'i' entries */
          pDescr += pDescr->i;
        }

        pDescr++;
        break;
      }

      case CSN_NEXT_EXIST_LH:
      {
        guint8 fExist;
        pui8  = pui8DATA(data, pDescr->offset);

        /* this if-statement represents the M_NEXT_EXIST_OR_NULL_LH description element */
        if ((pDescr->descr.ptr != NULL) && (remaining_bits_len == 0))
        { /* no more bits to decode is fine here - end of message detected and allowed */

          /* skip 'i' entries + this entry */
          pDescr += pDescr->i + 1;

          /* pDescr now must be pointing to a CSN_END entry, if not this is an error */
          if ( pDescr->type != CSN_END )
          { /* substract one more bit from remaining_bits_len to make the "not enough bits" error to be triggered */
            remaining_bits_len--;
          }

          /* set the data member to "not exist" */
          //*pui8 = 0;
          break;
        }

        /* the "regular" M_NEXT_EXIST_LH description element */
        vector->writeField(writeIndex, *pui8, 1);
        writeIndex--;
        fExist = get_masked_bits8(vector,writeIndex, bit_offset, 1);
        writeIndex--;
        vector->writeField(writeIndex, fExist, 1);
        pui8++;
        remaining_bits_len -= 1;

        bit_offset++;

        if (fExist == 0)
        { /* Skip 'i' entries */
          pDescr += pDescr->i;
        }
        pDescr++;

        break;
      }

      case CSN_VARIABLE_BITMAP_1:
      { /* Bitmap from here and to the end of message */

        //*pui8DATA(data, (gint16)pDescr->descr.value) = (guint8) remaining_bits_len; /* length of bitmap == remaining bits */

        /*no break -
         * with a length set we have a regular variable length bitmap so we continue */
      }

      case CSN_VARIABLE_BITMAP:
      { /* {CSN_VARIABLE_BITMAP, 0, offsetof(_STRUCT, _ElementCountField), offsetof(_STRUCT, _MEMBER), #_MEMBER}
         * <N: bit (5)> <bitmap: bit(N + offset)>
         * Bit array with length (in bits) specified in parameter (pDescr->descr)
         * The result is right aligned!
         */
        gint16 no_of_bits = *pui8DATA(data, (gint16)pDescr->descr.value);

        no_of_bits += pDescr->i; /* adjusted by offset */

        if (no_of_bits > 0)
        {
          remaining_bits_len -= no_of_bits;

          if (remaining_bits_len < 0)
          {
            return ProcessError(writeIndex,"csnStreamEncoder", CSN_ERROR_NEED_MORE_BITS_TO_UNPACK, pDescr);
          }

          { /* extract bits */
            guint8* pui8 = pui8DATA(data, pDescr->offset);
            gint16 nB1  = no_of_bits & 0x07;/* no_of_bits Mod 8 */

            if (nB1 > 0)
            { /* take care of the first byte - it will be right aligned */
              vector->writeField(writeIndex, *pui8, nB1);
              LOG(INFO) << pDescr->sz << " = " << (unsigned)*pui8 << "\n" ;
              pui8++;
              no_of_bits  -= nB1;
              bit_offset += nB1; /* (nB1 is no_of_bits Mod 8) */
            }

            /* remaining no_of_bits is a multiple of 8 or 0 */
            while (no_of_bits > 0)
            {
              vector->writeField(writeIndex, *pui8, 8);
              LOG(INFO) << pDescr->sz << " = " << (unsigned)*pui8 << "\n" ;
              pui8++;
              no_of_bits -= 8;
            }
          }
        }
        pDescr++;
        break;
      }

      case CSN_LEFT_ALIGNED_VAR_BMP_1:
      { /* Bitmap from here and to the end of message */

        //*pui8DATA(data, (gint16)pDescr->descr.value) = (guint8) remaining_bits_len; /* length of bitmap == remaining bits */

        /* no break -
         * with a length set we have a regular left aligned variable length bitmap so we continue
         */
      }

      case CSN_LEFT_ALIGNED_VAR_BMP:
      { /* {CSN_LEFT_ALIGNED_VAR_BMP, _OFFSET, (void*)offsetof(_STRUCT, _ElementCountField), offsetof(_STRUCT, _MEMBER), #_MEMBER}
         * <N: bit (5)> <bitmap: bit(N + offset)>
         * bit array with length (in bits) specified in parameter (pDescr->descr)
         */

        gint16 no_of_bits = *pui8DATA(data, (gint16)pDescr->descr.value);/* Size of bitmap */

        no_of_bits += pDescr->i;/* size adjusted by offset */

        if (no_of_bits > 0)
        {
          remaining_bits_len -= no_of_bits;

          if (remaining_bits_len < 0)
          {
            return ProcessError(writeIndex,"csnStreamEncoder", CSN_ERROR_NEED_MORE_BITS_TO_UNPACK, pDescr);
          }

          { /* extract bits */
            guint8* pui8 = pui8DATA(data, pDescr->offset);
            gint16 nB1  = no_of_bits & 0x07;/* no_of_bits Mod 8 */

            while (no_of_bits > 0)
            {
              vector->writeField(writeIndex, *pui8, 8);
              LOG(INFO) << pDescr->sz << " = " << (unsigned)*pui8 << "\n" ;
              pui8++;
              no_of_bits -= 8;
            }
            if (nB1 > 0)
            {
              vector->writeField(writeIndex, *pui8, nB1);
              LOG(INFO) << pDescr->sz << " = " << (unsigned)*pui8 << "\n" ;
              pui8++;
              no_of_bits  -= nB1;
              bit_offset += nB1; /* (nB1 is no_of_bits Mod 8) */
            }
          }

        }

        /* bitmap was successfully extracted or it was empty */
        pDescr++;
        break;
      }

      case CSN_VARIABLE_ARRAY:
      { /* {int type; int i; void* descr; int offset; const char* sz; } CSN_DESCR;
         * {CSN_VARIABLE_ARRAY, _OFFSET, (void*)offsetof(_STRUCT, _ElementCountField), offsetof(_STRUCT, _MEMBER), #_MEMBER}
         * Array with length specified in parameter:
         *  <count: bit (x)>
         *  <list: octet(count + offset)>
         */
        gint16 count = *pui8DATA(data, (gint16)pDescr->descr.value);

        count += pDescr->i; /* Adjusted by offset */

        if (count > 0)
        {
          remaining_bits_len -= count * 8;

          if (remaining_bits_len < 0)
          {
            return ProcessError(writeIndex,"csnStreamEncoder", CSN_ERROR_NEED_MORE_BITS_TO_UNPACK, pDescr);
          }

          pui8 = pui8DATA(data, pDescr->offset);

          while (count > 0)
          {
            vector->writeField(writeIndex, *pui8, 8);
            LOG(INFO) << pDescr->sz << " = " << (unsigned)*pui8 << "\n" ;
            pui8++;
            bit_offset += 8;
            count--;
          }
        }

        pDescr++;
        break;
      }

      case CSN_RECURSIVE_ARRAY:
      { /* Recursive way to specify an array: <list> ::= {1 <number: bit (4)> <list> | 0}
         *  or more generally:                <list> ::= { <tag> <element> <list> | <EndTag> }
         *  where <element> ::= bit(value)
         *        <tag>     ::= 0 | 1
         *        <EndTag>  ::= reversed tag i.e. tag == 1 -> EndTag == 0 and vice versa
         * {CSN_RECURSIVE_ARRAY, _BITS, (void*)offsetof(_STRUCT, _ElementCountField), offsetof(_STRUCT, _MEMBER), #_MEMBER}
         * REMARK: recursive way to specify an array but an iterative implementation!
         */
        gint16 no_of_bits        = pDescr->i;
        guint8  ElementCount = 0;
        pui8  = pui8DATA(data, pDescr->offset);
        ElementCount = *pui8DATA(data, (gint16)pDescr->descr.value);
        while (ElementCount > 0)
        { /* tag control shows existence of next list elements */
          vector->writeField(writeIndex, Tag, 1);
          LOG(INFO) << pDescr->sz << " = " << (unsigned)Tag << "\n" ;
          bit_offset++;
          remaining_bits_len--;

          /* extract and store no_of_bits long element from bitstream */
          vector->writeField(writeIndex, *pui8, no_of_bits);
          LOG(INFO) << pDescr->sz << " = " << (unsigned)*pui8 << "\n" ;
          pui8++;
          remaining_bits_len -= no_of_bits;
          ElementCount--;

          if (remaining_bits_len < 0)
          {
            return ProcessError(writeIndex,"csnStreamEncoder", CSN_ERROR_NEED_MORE_BITS_TO_UNPACK, pDescr);
          }

          bit_offset += no_of_bits;
        }

        vector->writeField(writeIndex, !Tag, 1);
        LOG(INFO) << pDescr->sz << " = " << (unsigned)(!Tag) << "\n" ;
        bit_offset++;

        pDescr++;
        break;
      }

      case CSN_RECURSIVE_TARRAY:
      { /* Recursive way to specify an array of type: <lists> ::= { 1 <type> } ** 0 ;
         *  M_REC_TARRAY(_STRUCT, _MEMBER, _MEMBER_TYPE, _ElementCountField)
         * {t, offsetof(_STRUCT, _ElementCountField), (void*)CSNDESCR_##_MEMBER_TYPE, offsetof(_STRUCT, _MEMBER), #_MEMBER, (StreamSerializeFcn_t)sizeof(_MEMBER_TYPE)}
         */
        gint16 nSizeElement = (gint16)(gint32)pDescr->serialize.value;
        guint8  ElementCount = 0;
        pui8  = pui8DATA(data, pDescr->offset);
        /* Store the counted number of elements of the array */
        ElementCount = *pui8DATA(data, (gint16)(gint32)pDescr->i);

        while (ElementCount > 0)
        { /* tag control shows existence of next list elements */
          vector->writeField(writeIndex, Tag, 1);
          LOG(INFO) << pDescr->sz << " = " << (unsigned)Tag << "\n" ;
          bit_offset++;

          remaining_bits_len--;
          ElementCount--;

          { /* unpack the following data structure */
            csnStream_t arT = *ar;
            gint16      Status;
            csnStreamInit(&arT, bit_offset, remaining_bits_len);
            Status = csnStreamEncoder(&arT, (const CSN_DESCR*)pDescr->descr.ptr, vector, writeIndex, pvDATA(data, pDescr->offset));

            if (Status >= 0)
            { /* successful completion */
              pui8    += nSizeElement;  /* -> to next data element */
              remaining_bits_len = arT.remaining_bits_len;
              bit_offset         = arT.bit_offset;
            }
            else
            { /* something went awry */
              return Status;
            }
          }

          if (remaining_bits_len < 0)
          {
            return ProcessError(writeIndex,"csnStreamEncoder", CSN_ERROR_NEED_MORE_BITS_TO_UNPACK, pDescr);
          }
        }

        vector->writeField(writeIndex, !Tag, 1);
        LOG(INFO) << pDescr->sz << " = " << (unsigned)(!Tag) << "\n" ;
        bit_offset++;

        pDescr++;
        break;
      }

      case CSN_RECURSIVE_TARRAY_2:
      { /* Recursive way to specify an array of type: <list> ::= <type> { 0 <type> } ** 1 ; */

        Tag = REVERSED_TAG;

        /* NO break -
         * handling is exactly the same as for CSN_RECURSIVE_TARRAY_1 so we continue
         */
      }

      case CSN_RECURSIVE_TARRAY_1:
      { /* Recursive way to specify an array of type: <lists> ::= <type> { 1 <type> } ** 0 ;
         * M_REC_TARRAY(_STRUCT, _MEMBER, _MEMBER_TYPE, _ElementCountField)
         * {t, offsetof(_STRUCT, _ElementCountField), (void*)CSNDESCR_##_MEMBER_TYPE, offsetof(_STRUCT, _MEMBER), #_MEMBER, (StreamSerializeFcn_t)sizeof(_MEMBER_TYPE)}
         */
        gint16      nSizeElement = (gint16)(gint32)pDescr->serialize.value;
        guint8      ElementCount = 0;
        guint8      ElementNum   = 0;
        csnStream_t arT          = *ar;
        gint16      Status;

        pui8  = pui8DATA(data, pDescr->offset);
        /* Store the count of the array */
        ElementCount = *pui8DATA(data, pDescr->i);
        ElementNum = ElementCount;

        while (ElementCount > 0)
        { /* get data element */
          if (ElementCount != ElementNum)
          {
            vector->writeField(writeIndex, Tag, 1);
            LOG(INFO) << pDescr->sz << " = " << (unsigned)Tag << "\n" ;
            bit_offset++;
            remaining_bits_len--;
          }
          ElementCount--;
          LOG(INFO) << pDescr->sz << " { \n"; 
          csnStreamInit(&arT, bit_offset, remaining_bits_len);
          Status = csnStreamEncoder(&arT, (const CSN_DESCR*)pDescr->descr.ptr, vector, writeIndex, pvDATA(data, pDescr->offset));
          LOG(INFO) << pDescr->sz << " } \n"; 
          if (Status >= 0)
          { /* successful completion */
            pui8    += nSizeElement;  /* -> to next */
            remaining_bits_len = arT.remaining_bits_len;
            bit_offset         = arT.bit_offset;
          }
          else
          { /* something went awry */
            return Status;
          }

          if (remaining_bits_len < 0)
          {
            return ProcessError(writeIndex,"csnStreamEncoder", CSN_ERROR_NEED_MORE_BITS_TO_UNPACK, pDescr);
          }

        }
        vector->writeField(writeIndex, !Tag, 1);
        bit_offset++;
        Tag = STANDARD_TAG; /* in case it was set to "reversed" */
        pDescr++;
        break;
      }

      case CSN_FIXED:
      { /* Verify the fixed bits */
        guint8  no_of_bits = (guint8) pDescr->i;
        vector->writeField(writeIndex, pDescr->offset, no_of_bits);
        LOG(INFO) << pDescr->sz<< " = " << pDescr->offset << "\n"; 
        remaining_bits_len   -= no_of_bits;
        bit_offset += no_of_bits;
        pDescr++;
        break;
      }

      case CSN_CALLBACK:
      {
        return ProcessError(writeIndex,"csnStreamEncoder Callback not implemented", -1, pDescr);
        break;
      }

      case CSN_TRAP_ERROR:
      {
        return ProcessError(writeIndex,"csnStreamEncoder", pDescr->i, pDescr);
      }

      case CSN_END:
      {
        ar->remaining_bits_len  = remaining_bits_len;
        ar->bit_offset = bit_offset;
        return remaining_bits_len;
      }

      default:
      {
        assert(0);
      }

    }

  } while (remaining_bits_len >= 0);

  return ProcessError(writeIndex,"csnStreamEncoder", CSN_ERROR_NEED_MORE_BITS_TO_UNPACK, pDescr);
}

