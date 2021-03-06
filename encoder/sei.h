/*****************************************************************************
* Copyright (C) 2013 x265 project
*
* Authors: Steve Borho <steve@borho.org>
*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation; either version 2 of the License, or
* (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program; if not, write to the Free Software
* Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02111, USA.
*
* This program is also available under a commercial proprietary license.
* For more information, contact us at license @ x265.com.
*****************************************************************************/

#ifndef X265_SEI_H
#define X265_SEI_H

#include "common.h"
#include "bitstream.h"
#include "TLibCommon/TComSlice.h"
#include "TLibEncoder/SyntaxElementWriter.h"

namespace x265 {
// private namespace

class SEI : public SyntaxElementWriter
{
public:

    /* SEI users call write() to marshal an SEI to a bitstream. SEI
     * subclasses may implement write() or accept the default write()
     * method which calls writeSEI() with a bitcounter to determine
     * the size, then it encodes the header and calls writeSEI a
     * second time for the real encode. */
    virtual void write(Bitstream& bs, TComSPS& sps);

    virtual ~SEI() {}

protected:

    enum PayloadType
    {
        BUFFERING_PERIOD                     = 0,
        PICTURE_TIMING                       = 1,
        PAN_SCAN_RECT                        = 2,
        FILLER_PAYLOAD                       = 3,
        USER_DATA_REGISTERED_ITU_T_T35       = 4,
        USER_DATA_UNREGISTERED               = 5,
        RECOVERY_POINT                       = 6,
        SCENE_INFO                           = 9,
        FULL_FRAME_SNAPSHOT                  = 15,
        PROGRESSIVE_REFINEMENT_SEGMENT_START = 16,
        PROGRESSIVE_REFINEMENT_SEGMENT_END   = 17,
        FILM_GRAIN_CHARACTERISTICS           = 19,
        POST_FILTER_HINT                     = 22,
        TONE_MAPPING_INFO                    = 23,
        FRAME_PACKING                        = 45,
        DISPLAY_ORIENTATION                  = 47,
        SOP_DESCRIPTION                      = 128,
        ACTIVE_PARAMETER_SETS                = 129,
        DECODING_UNIT_INFO                   = 130,
        TEMPORAL_LEVEL0_INDEX                = 131,
        DECODED_PICTURE_HASH                 = 132,
        SCALABLE_NESTING                     = 133,
        REGION_REFRESH_INFO                  = 134,
    };

    virtual PayloadType payloadType() const = 0;

    virtual void writeSEI(TComSPS&) {}

    void writeByteAlign();
};

#if ENC_DEC_TRACE
#define LOG(string) fprintf(g_hTrace, string)
#else
#define LOG(string)
#endif

class SEIDecodedPictureHash : public SEI
{
public:

    PayloadType payloadType() const { return DECODED_PICTURE_HASH; }

    enum Method
    {
        MD5,
        CRC,
        CHECKSUM,
    } m_method;

    uint8_t m_digest[3][16];

    void write(Bitstream& bs, TComSPS&)
    {
        setBitstream(&bs);

        LOG("=========== Decoded picture hash SEI message ===========\n");

        WRITE_CODE(DECODED_PICTURE_HASH, 8, "payload_type");

        switch (m_method)
        {
        case MD5:
            WRITE_CODE(1 + 16 * 3, 8, "payload_size");
            WRITE_CODE(MD5, 8, "hash_type");
            break;
        case CRC:
            WRITE_CODE(1 + 2 * 3, 8, "payload_size");
            WRITE_CODE(CRC, 8, "hash_type");
            break;
        case CHECKSUM:
            WRITE_CODE(1 + 4 * 3, 8, "payload_size");
            WRITE_CODE(CHECKSUM, 8, "hash_type");
            break;
        }

        for (int yuvIdx = 0; yuvIdx < 3; yuvIdx++)
        {
            if (m_method == MD5)
            {
                for (uint32_t i = 0; i < 16; i++)
                    WRITE_CODE(m_digest[yuvIdx][i], 8, "picture_md5");
            }
            else if (m_method == CRC)
            {
                uint32_t val = (m_digest[yuvIdx][0] << 8) + m_digest[yuvIdx][1];
                WRITE_CODE(val, 16, "picture_crc");
            }
            else if (m_method == CHECKSUM)
            {
                uint32_t val = (m_digest[yuvIdx][0] << 24) + (m_digest[yuvIdx][1] << 16) + (m_digest[yuvIdx][2] << 8) + m_digest[yuvIdx][3];
                WRITE_CODE(val, 32, "picture_checksum");
            }
        }
    }
};

class SEIActiveParameterSets : public SEI
{
public:

    PayloadType payloadType() const { return ACTIVE_PARAMETER_SETS; }

    int  m_activeVPSId;
    int  m_numSpsIdsMinus1;
    int  m_activeSeqParamSetId;
    bool m_fullRandomAccessFlag;
    bool m_noParamSetUpdateFlag;

    void writeSEI(TComSPS&)
    {
        LOG("=========== Active Parameter sets SEI message ===========\n");
        WRITE_CODE(m_activeVPSId,     4,   "active_vps_id");
        WRITE_FLAG(m_fullRandomAccessFlag, "full_random_access_flag");
        WRITE_FLAG(m_noParamSetUpdateFlag, "no_param_set_update_flag");
        WRITE_UVLC(m_numSpsIdsMinus1,      "num_sps_ids_minus1");
        WRITE_UVLC(m_activeSeqParamSetId,  "active_seq_param_set_id");
        writeByteAlign();
    }
};

class SEIBufferingPeriod : public SEI
{
public:

    PayloadType payloadType() const { return BUFFERING_PERIOD; }

    SEIBufferingPeriod()
        : m_bpSeqParameterSetId(0)
        , m_rapCpbParamsPresentFlag(false)
        , m_cpbDelayOffset(0)
        , m_dpbDelayOffset(0)
    {
        ::memset(m_initialCpbRemovalDelay, 0, sizeof(m_initialCpbRemovalDelay));
        ::memset(m_initialCpbRemovalDelayOffset, 0, sizeof(m_initialCpbRemovalDelayOffset));
        ::memset(m_initialAltCpbRemovalDelay, 0, sizeof(m_initialAltCpbRemovalDelay));
        ::memset(m_initialAltCpbRemovalDelayOffset, 0, sizeof(m_initialAltCpbRemovalDelayOffset));
    }

    uint32_t m_bpSeqParameterSetId;
    bool     m_rapCpbParamsPresentFlag;
    bool     m_cpbDelayOffset;
    bool     m_dpbDelayOffset;
    uint32_t m_initialCpbRemovalDelay[MAX_CPB_CNT][2];
    uint32_t m_initialCpbRemovalDelayOffset[MAX_CPB_CNT][2];
    uint32_t m_initialAltCpbRemovalDelay[MAX_CPB_CNT][2];
    uint32_t m_initialAltCpbRemovalDelayOffset[MAX_CPB_CNT][2];
    bool     m_concatenationFlag;
    uint32_t m_auCpbRemovalDelayDelta;

    void writeSEI(TComSPS& sps)
    {
        TComVUI *vui = sps.getVuiParameters();
        TComHRD *hrd = vui->getHrdParameters();

        LOG("=========== Buffering period SEI message ===========\n");

        WRITE_UVLC(m_bpSeqParameterSetId, "bp_seq_parameter_set_id");
        if (!hrd->getSubPicHrdParamsPresentFlag())
        {
            WRITE_FLAG(m_rapCpbParamsPresentFlag, "rap_cpb_params_present_flag");
        }
        WRITE_FLAG(m_concatenationFlag, "concatenation_flag");
        WRITE_CODE(m_auCpbRemovalDelayDelta - 1, (hrd->getCpbRemovalDelayLengthMinus1() + 1), "au_cpb_removal_delay_delta_minus1");
        if (m_rapCpbParamsPresentFlag)
        {
            WRITE_CODE(m_cpbDelayOffset, hrd->getCpbRemovalDelayLengthMinus1() + 1, "cpb_delay_offset");
            WRITE_CODE(m_dpbDelayOffset, hrd->getDpbOutputDelayLengthMinus1()  + 1, "dpb_delay_offset");
        }
        for (int nalOrVcl = 0; nalOrVcl < 2; nalOrVcl++)
        {
            if (((nalOrVcl == 0) && (hrd->getNalHrdParametersPresentFlag())) ||
                ((nalOrVcl == 1) && (hrd->getVclHrdParametersPresentFlag())))
            {
                for (uint32_t i = 0; i < (hrd->getCpbCntMinus1(0) + 1); i++)
                {
                    WRITE_CODE(m_initialCpbRemovalDelay[i][nalOrVcl], (hrd->getInitialCpbRemovalDelayLengthMinus1() + 1),           "initial_cpb_removal_delay");
                    WRITE_CODE(m_initialCpbRemovalDelayOffset[i][nalOrVcl], (hrd->getInitialCpbRemovalDelayLengthMinus1() + 1),      "initial_cpb_removal_delay_offset");
                    if (hrd->getSubPicHrdParamsPresentFlag() || m_rapCpbParamsPresentFlag)
                    {
                        WRITE_CODE(m_initialAltCpbRemovalDelay[i][nalOrVcl], (hrd->getInitialCpbRemovalDelayLengthMinus1() + 1),     "initial_alt_cpb_removal_delay");
                        WRITE_CODE(m_initialAltCpbRemovalDelayOffset[i][nalOrVcl], (hrd->getInitialCpbRemovalDelayLengthMinus1() + 1), "initial_alt_cpb_removal_delay_offset");
                    }
                }
            }
        }

        writeByteAlign();
    }
};

class SEIPictureTiming : public SEI
{
public:

    PayloadType payloadType() const { return PICTURE_TIMING; }

    uint32_t  m_picStruct;
    uint32_t  m_sourceScanType;
    bool      m_duplicateFlag;

    uint32_t  m_auCpbRemovalDelay;
    uint32_t  m_picDpbOutputDelay;

    void writeSEI(TComSPS& sps)
    {
        LOG("=========== Picture timing SEI message ===========\n");

        TComVUI *vui = sps.getVuiParameters();
        TComHRD *hrd = vui->getHrdParameters();

        if (vui->getFrameFieldInfoPresentFlag())
        {
            WRITE_CODE(m_picStruct, 4,          "pic_struct");
            WRITE_CODE(m_sourceScanType, 2,     "source_scan_type");
            WRITE_FLAG(m_duplicateFlag ? 1 : 0, "duplicate_flag");
        }

        if (hrd->getCpbDpbDelaysPresentFlag())
        {
            WRITE_CODE(m_auCpbRemovalDelay - 1, (hrd->getCpbRemovalDelayLengthMinus1() + 1), "au_cpb_removal_delay_minus1");
            WRITE_CODE(m_picDpbOutputDelay, (hrd->getDpbOutputDelayLengthMinus1() + 1), "pic_dpb_output_delay");
            /* Removed sub-pic signaling June 2014 */
        }
        writeByteAlign();
    }
};

class SEIRecoveryPoint : public SEI
{
public:

    PayloadType payloadType() const { return RECOVERY_POINT; }

    int  m_recoveryPocCnt;
    bool m_exactMatchingFlag;
    bool m_brokenLinkFlag;

    void writeSEI(TComSPS&)
    {
        LOG("=========== Recovery point SEI message ===========\n");
        WRITE_SVLC(m_recoveryPocCnt,    "recovery_poc_cnt");
        WRITE_FLAG(m_exactMatchingFlag, "exact_matching_flag");
        WRITE_FLAG(m_brokenLinkFlag,    "broken_link_flag");
        writeByteAlign();
    }
};
}

#undef LOG

#endif // ifndef X265_SEI_H
