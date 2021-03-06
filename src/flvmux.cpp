/*
 * =====================================================================================
 *
 *    Filename   :  flv_mux.cpp
 *    Description:
 *    Version    :  1.0
 *    Created    :  2016��06��30�� 13ʱ00��09��
 *    Revision   :  none
 *    Compiler   :  gcc
 *    Author     :  peter-s
 *    Email      :  peter_future@outlook.com
 *    Company    :  dt
 *
 * =====================================================================================
 */

#include "flvmux_api.h"
#include "rtmp.h"

#include "log_print.h"
#define TAG "FLVMUX"

// @brief start publish
// @param [in] rtmp_sender handler
// @param [in] flag        stream falg
// @param [in] ts_us       timestamp in us
// @return             : 0: OK; others: FAILED
static const AVal av_onMetaData = AVC("onMetaData");
static const AVal av_duration = AVC("duration");
static const AVal av_width = AVC("width");
static const AVal av_height = AVC("height");
static const AVal av_videocodecid = AVC("videocodecid");
static const AVal av_avcprofile = AVC("avcprofile");
static const AVal av_avclevel = AVC("avclevel");
static const AVal av_videoframerate = AVC("videoframerate");
static const AVal av_audiocodecid = AVC("audiocodecid");
static const AVal av_audiosamplerate = AVC("audiosamplerate");
static const AVal av_audiochannels = AVC("audiochannels");
static const AVal av_avc1 = AVC("avc1");
static const AVal av_mp4a  = AVC("mp4a");
static const AVal av_onPrivateData = AVC("onPrivateData");
static const AVal av_record = AVC("record");

struct flvmux_context *flvmux_open(struct flvmux_para *para)
{
    struct flvmux_context *handle = (struct flvmux_context *)malloc(sizeof(struct flvmux_context));
    if (!handle) {
        return NULL;
    }
    memset(handle, 0, sizeof(struct flvmux_context));

    memcpy(&handle->para, para, sizeof(struct flvmux_para));
    int64_t ts_us = 0;

#if 0 // Used for save FLV File 
    // setup FLV Header
    char flv_file_header[] = "FLV\x1\x5\0\0\0\x9\0\0\0\0"; // have audio and have video
    if (para->has_video && para->has_audio) {
        flv_file_header[4] = 0x05;
    } else if (para->has_audio && !para->has_video) {
        flv_file_header[4] = 0x04;
    } else if (!para->has_audio && para->has_video) {
        flv_file_header[4] = 0x01;
    } else {
        flv_file_header[4] = 0x00;
    }

    memcpy(handle->header, flv_file_header, 13);
    handle->header_size += 13;
#endif

    // setup flv header
    uint32_t body_len;
    uint32_t offset = 0;
    uint32_t output_len;
    char buffer[512];
    char *output = buffer;
    char *outend = buffer + sizeof(buffer);
    char send_buffer[512];

    output = AMF_EncodeString(output, outend, &av_onMetaData);
    *output++ = AMF_ECMA_ARRAY;
    output = AMF_EncodeInt32(output, outend, 3);
    output = AMF_EncodeNamedNumber(output, outend, &av_duration, 0.0);
    output = AMF_EncodeNamedNumber(output, outend, &av_videocodecid, 7);
    output = AMF_EncodeNamedNumber(output, outend, &av_audiocodecid, 10);
    output = AMF_EncodeInt24(output, outend, AMF_OBJECT_END);

    body_len = output - buffer;
    output_len = body_len + FLV_TAG_HEAD_LEN + FLV_PRE_TAG_LEN;
    send_buffer[offset++] = 0x12; //tagtype metadata
    send_buffer[offset++] = (uint8_t)(body_len >> 16); //data len
    send_buffer[offset++] = (uint8_t)(body_len >> 8); //data len
    send_buffer[offset++] = (uint8_t)(body_len); //data len
    send_buffer[offset++] = (uint8_t)(ts_us >> 16); //time stamp
    send_buffer[offset++] = (uint8_t)(ts_us >> 8); //time stamp
    send_buffer[offset++] = (uint8_t)(ts_us); //time stamp
    send_buffer[offset++] = (uint8_t)(ts_us >> 24); //time stamp
    send_buffer[offset++] = 0x00; //stream id 0
    send_buffer[offset++] = 0x00; //stream id 0
    send_buffer[offset++] = 0x00; //stream id 0

    memcpy(send_buffer + offset, buffer, body_len); //H264 sequence parameter set
    memcpy(handle->header + handle->header_size, send_buffer, output_len);
    handle->header_size += output_len;

    log_print(TAG, "FLVMUX Open ok\n");
    return handle;
}

static AudioSpecificConfig gen_config(uint8_t *frame)
{
    AudioSpecificConfig config = {0, 0, 0};

    if (frame == NULL) {
        return config;
    }
    config.audio_object_type = (frame[2] & 0xc0) >> 6;
    config.sample_frequency_index =  (frame[2] & 0x3c) >> 2;
    config.channel_configuration = (frame[3] & 0xc0) >> 6;
    log_print(TAG, "Gen config: type:%d frequency:%d \n", (int)config.audio_object_type, (int)config.sample_frequency_index);
    return config;
}

static uint8_t gen_audio_tag_header(AudioSpecificConfig config)
{
     uint8_t soundType = config.channel_configuration - 1; //0 mono, 1 stero
     uint8_t soundRate = 0;
     uint8_t val = 0;


     switch (config.sample_frequency_index) {
         case 10: { //11.025k
             soundRate = 1;
             break;
         }
         case 7: { //22k
             soundRate = 2;
             break;
         }
         case 4: { //44k
             soundRate = 3;
             break;
         }
         case 3: { //48k not support but for debug mode
             soundRate = 3;
             break;
         }
         default:
         { 
             return val;
         }
    }
    val = 0xA0 | (soundRate << 2) | 0x02 | soundType;
    log_print(TAG, "soundType:%d soundRate:%d Val:%x \n", (int)soundType, (int)soundRate, val);
    return val;
}

static uint8_t *get_adts(uint32_t *len, uint8_t **offset, uint8_t *start, uint32_t total)
{
    uint8_t *p  =  *offset;
    uint32_t frame_len_1;
    uint32_t frame_len_2;
    uint32_t frame_len_3;
    uint32_t frame_length;
   
    if (total < AAC_ADTS_HEADER_SIZE) {
        return NULL;
    }
    if ((p - start) >= total) {
        return NULL;
    }
    
    if (p[0] != 0xff) {
        return NULL;
    }
    if ((p[1] & 0xf0) != 0xf0) {
        return NULL;
    }
    frame_len_1 = p[3] & 0x03;
    frame_len_2 = p[4];
    frame_len_3 = (p[5] & 0xe0) >> 5;
    frame_length = (frame_len_1 << 11) | (frame_len_2 << 3) | frame_len_3;
    *offset = p + frame_length;
    *len = frame_length;
    //log_print(TAG, "Find adts header: size:%u total:%u \n", (int)(*offset - start), total);
    return p;
}

int flvmux_setup_audio_frame(struct flvmux_context *handle, struct flvmux_packet *in, struct flvmux_packet *out)
{
    uint32_t audio_ts = (uint32_t)in->pts;
    uint8_t * audio_buf = in->data; 
    uint32_t audio_total = in->size;
    uint8_t *audio_buf_offset = audio_buf;
    uint8_t *audio_frame;
    uint32_t adts_len;
    uint32_t offset;
    uint32_t body_len;
    uint32_t output_len;
    char *output ; 

    uint8_t *config_buf = NULL;
    int config_size = 0;

PARSE_BEGIN:
    offset = 0;
    audio_frame = get_adts(&adts_len, &audio_buf_offset, audio_buf, audio_total);
    if (audio_frame == NULL)
        return -1;
    log_print(TAG, "Config:%d \n", handle->audio_config_ok);
    if (handle->audio_config_ok == 0) {
        handle->config = gen_config(audio_frame);
        body_len = 2 + 2; //AudioTagHeader + AudioSpecificConfig
        output_len = body_len + FLV_TAG_HEAD_LEN + FLV_PRE_TAG_LEN;
        output = (char *)malloc(output_len);
        // flv tag header
        output[offset++] = 0x08; //tagtype audio
        output[offset++] = (uint8_t)(body_len >> 16); //data len
        output[offset++] = (uint8_t)(body_len >> 8); //data len
        output[offset++] = (uint8_t)(body_len); //data len
        output[offset++] = (uint8_t)(audio_ts >> 16); //time stamp
        output[offset++] = (uint8_t)(audio_ts >> 8); //time stamp
        output[offset++] = (uint8_t)(audio_ts); //time stamp
        output[offset++] = (uint8_t)(audio_ts >> 24); //time stamp
        output[offset++] = 0; //stream id 0
        output[offset++] = 0x00; //stream id 0
        output[offset++] = 0x00; //stream id 0

        //flv AudioTagHeader
        output[offset++] = gen_audio_tag_header(handle->config); // sound format aac
        output[offset++] = 0x00; //aac sequence header

        //flv VideoTagBody --AudioSpecificConfig
        uint8_t audio_object_type = handle->config.audio_object_type + 1;
        output[offset++] = (audio_object_type << 3)|(handle->config.sample_frequency_index >> 1); 
        output[offset++] = ((handle->config.sample_frequency_index & 0x01) << 7) \
                           | (handle->config.channel_configuration << 3) ;
        //no need to set pre_tag_size
        uint32_t fff = body_len + FLV_TAG_HEAD_LEN;
        output[offset++] = (uint8_t)(fff >> 24); //data len
        output[offset++] = (uint8_t)(fff >> 16); //data len
        output[offset++] = (uint8_t)(fff >> 8); //data len
        output[offset++] = (uint8_t)(fff); //data len

        config_buf = (uint8_t *)output;
        if(!config_buf)
            return -1;
        config_size = output_len;
        handle->audio_config_ok = 1;
        // Handle Real Frame
        audio_buf_offset = audio_frame;
        output_len = 0;
        goto PARSE_BEGIN;
    } else {
        //body_len = 2 + adts_len - AAC_ADTS_HEADER_SIZE; // remove adts header + AudioTagHeader
        body_len = 2 + adts_len; 
        output_len = body_len + FLV_TAG_HEAD_LEN + FLV_PRE_TAG_LEN;
        output = (char *)malloc(output_len);
        // flv tag header
        output[offset++] = 0x08; //tagtype audio
        output[offset++] = (uint8_t)(body_len >> 16); //data len
        output[offset++] = (uint8_t)(body_len >> 8); //data len
        output[offset++] = (uint8_t)(body_len); //data len
        output[offset++] = (uint8_t)(audio_ts >> 16); //time stamp
        output[offset++] = (uint8_t)(audio_ts >> 8); //time stamp
        output[offset++] = (uint8_t)(audio_ts); //time stamp
        output[offset++] = (uint8_t)(audio_ts >> 24); //time stamp
        output[offset++] = 0; //stream id 0
        output[offset++] = 0x00; //stream id 0
        output[offset++] = 0x00; //stream id 0

        //flv AudioTagHeader
        output[offset++] = gen_audio_tag_header(handle->config); // sound format aac
        output[offset++] = 0x01; //aac raw data 

        //flv VideoTagBody --raw aac data
        //memcpy(output + offset, audio_frame + AAC_ADTS_HEADER_SIZE, (adts_len - AAC_ADTS_HEADER_SIZE)); //H264 sequence parameter set
        memcpy(output + offset, audio_frame, adts_len);
        
        //previous tag size 
        uint32_t fff = body_len + FLV_TAG_HEAD_LEN;
        //offset += (adts_len - AAC_ADTS_HEADER_SIZE);
        offset += adts_len;
        output[offset++] = (uint8_t)(fff >> 24); //data len
        output[offset++] = (uint8_t)(fff >> 16); //data len
        output[offset++] = (uint8_t)(fff >> 8); //data len
        output[offset++] = (uint8_t)(fff); //data len

    }

    out->data = (uint8_t *)malloc(config_size + output_len);
    if(!out->data)
        return -1;
    out->size = output_len + config_size;
    if(config_size > 0) {
        memcpy(out->data, config_buf, config_size);
        free(config_buf);
    }
    if(output_len > 0) {
        memcpy(out->data + config_size, output, output_len);
        free(output);
    }
    out->pts = in->pts;
    out->type = 1;
    log_print(TAG, "Data[%02x %02x %02x %02x]\n", out->data[0], out->data[1], out->data[2], out->data[3]);
    log_print(TAG, "Setup AAC Audio Frame size:%d out->data:%p\n", out->size, out->data);
    
    return out->size;
}

static uint8_t *h264_find_IDR_frame(char *buffer, int total)
{
    uint8_t *buf = (uint8_t *)buffer;
    while (total > 4) {
        if (buf[0] == 0x00 && buf[1] == 0x00 && buf[2] == 0x01) {
            // Found a NAL unit with 3-byte startcode
            if (buf[3] & 0x1F == 0x5) {
                // Found a reference frame, do something with it
            }
            break;
        } else if (buf[0] == 0x00 && buf[1] == 0x00 && buf[2] == 0x00 && buf[3] == 0x01) {
            // Found a NAL unit with 4-byte startcode
            if (buf[4] & 0x1F == 0x5) {
                // Found a reference frame, do something with it
            }
            break;
        }
        buf++;
        total--;
    }

    if (total <= 4) {
        return NULL;
    }
    return buf;
}

static uint8_t *h264_find_NAL(uint8_t *buffer, int total, int *startcode_len)
{
    uint8_t *buf = (uint8_t *)buffer;
    while (total > 4) {
        
        if (buf[0] == 0x00 && buf[1] == 0x00 && buf[2] == 0x01) {
            // Found a NAL unit with 3-byte startcode
            if (buf[3] & 0x1F == 0x5) {
                // Found a reference frame, do something with it
            }
            *startcode_len = 3;
            break;
        } else if (buf[0] == 0x00 && buf[1] == 0x00 && buf[2] == 0x00 && buf[3] == 0x01) {
            // Found a NAL unit with 4-byte startcode
            if (buf[4] & 0x1F == 0x5) {
                // Found a reference frame, do something with it
            }
            *startcode_len = 4;
            break;
        }
        buf++;
        total--;
    }

    if (total <= 4) {
        return NULL;
    }
    return buf;
}

// Complete Frame Process
int flvmux_setup_video_frame(struct flvmux_context *handle, struct flvmux_packet *in, struct flvmux_packet *out)
{
    int sps_len = 0;
    uint8_t *sps_buf = NULL;
    int frame_len = 0;
    uint8_t *frame_buf = NULL;

    uint8_t *buf_264 = (uint8_t *)in->data;
    uint32_t total_264 = (uint32_t)in->size;
    uint8_t *vbuf_start = buf_264;
    uint8_t *vbuf_end = buf_264 + total_264;
    uint8_t *vbuf_off = buf_264;

    uint8_t *nal, *nal_pps, *nal_frame, *nal_next;
    uint32_t nal_len, nal_pps_len, nal_frame_len;

    uint8_t *output = NULL;
    uint32_t body_len;
    uint32_t output_len;

    uint32_t ts = (uint32_t)in->pts;
    uint32_t offset = 0;

    int startcode_len = 0;

    log_print(TAG, "Total Pakcet size:%u \n", total_264);
PARSE_BEGIN:
    // Find Nal. Maybe SPS
    offset = 0;
    log_print(TAG, "Curpos:%d %02x %02x %02x %02x %02x\n", vbuf_off - vbuf_start, vbuf_off[0], vbuf_off[1], vbuf_off[2], vbuf_off[3], vbuf_off[4]);
    nal = h264_find_NAL(vbuf_off, vbuf_start + total_264 - vbuf_off, &startcode_len);
    if (nal == NULL) {
        goto end;
    }

    vbuf_off = nal + startcode_len;
    if (nal[startcode_len] == 0x67)  {

        if (handle->video_config_ok == 1) {
            log_print(TAG, "I frame configured \n");
            //return -1;
        }

        nal_pps = h264_find_NAL(vbuf_off, vbuf_start + total_264 - vbuf_off, &startcode_len);
        nal_len = nal_pps - nal - 4;
        log_print(TAG, "nal: %02x %d pos:%d\n", nal[4], nal_len, nal - vbuf_start);
        vbuf_off = nal_pps + startcode_len;
        nal_frame = h264_find_NAL(vbuf_off, vbuf_start + total_264 - vbuf_off, &startcode_len);
        nal_pps_len = nal_frame - nal_pps - 4;
        log_print(TAG, "pps: %02x %d pos:%d \n", nal_pps[4], nal_pps_len, nal_pps - vbuf_start);
        vbuf_off = nal_frame;

        body_len = nal_len + nal_pps_len + 16;
        output_len = body_len + FLV_TAG_HEAD_LEN + FLV_PRE_TAG_LEN;
        output = (uint8_t *)malloc(output_len);

        // FLV TAG HEADER
        output[offset++] = 0x09; //tagtype video
        output[offset++] = (uint8_t)(body_len >> 16); //data len
        output[offset++] = (uint8_t)(body_len >> 8); //data len
        output[offset++] = (uint8_t)(body_len); //data len
        output[offset++] = (uint8_t)(ts >> 16); //time stamp
        output[offset++] = (uint8_t)(ts >> 8); //time stamp
        output[offset++] = (uint8_t)(ts); //time stamp
        output[offset++] = (uint8_t)(ts >> 24); //time stamp
        output[offset++] = in->pts; //stream id 0
        output[offset++] = 0x00; //stream id 0
        output[offset++] = 0x00; //stream id 0

        //FLV VideoTagHeader
        output[offset++] = 0x17; //key frame, AVC
        output[offset++] = 0x00; //avc sequence header
        output[offset++] = 0x00; //composit time
        output[offset++] = 0x00; // composit time
        output[offset++] = 0x00; //composit time

        //flv VideoTagBody --AVCDecoderCOnfigurationRecord
        output[offset++] = 0x01; //configurationversion
        output[offset++] = nal[1]; //avcprofileindication
        output[offset++] = nal[2]; //profilecompatibilty
        output[offset++] = nal[3]; //avclevelindication
        output[offset++] = 0xff; //reserved + lengthsizeminusone
        output[offset++] = 0xe1; //numofsequenceset
        output[offset++] = (uint8_t)(nal_len >> 8); //sequence parameter set length high 8 bits
        output[offset++] = (uint8_t)(nal_len); //sequence parameter set  length low 8 bits
        memcpy(output + offset, nal + 4, nal_len); //H264 sequence parameter set
        offset += nal_len;
        output[offset++] = 0x01; //numofpictureset
        output[offset++] = (uint8_t)(nal_pps_len >> 8); //picture parameter set length high 8 bits
        output[offset++] = (uint8_t)(nal_pps_len); //picture parameter set length low 8 bits
        memcpy(output + offset, nal_pps + 4, nal_pps_len); //H264 picture parameter set

        offset += nal_pps_len;
        uint32_t fff = body_len + FLV_TAG_HEAD_LEN;
        output[offset++] = (uint8_t)(fff >> 24); //data len
        output[offset++] = (uint8_t)(fff >> 16); //data len
        output[offset++] = (uint8_t)(fff >> 8); //data len
        output[offset++] = (uint8_t)(fff); //data len

        sps_len = output_len;
        sps_buf = output;
#if 0
        out->size = output_len;
        out->data = (uint8_t *)malloc(output_len);
        memcpy(out->data, output, output_len);
        free(output);
#endif
        handle->video_config_ok = 1;
        goto PARSE_BEGIN;
    } else if (nal[startcode_len] == 0x65 || (nal[startcode_len] & 0x1f) == 0x01) {
        nal_len = vbuf_end - nal - startcode_len;
        //log_print(TAG, "nal len:%d \n", nal_len);
        body_len = nal_len + 5 + 4; //flv VideoTagHeader +  NALU length
        output_len = body_len + FLV_TAG_HEAD_LEN + FLV_PRE_TAG_LEN;
        output = (uint8_t *)malloc(output_len);
        if (!output) {
            return -1;
        }

        // FLV TAG HEADER
        output[offset++] = 0x09; //tagtype video
        output[offset++] = (uint8_t)(body_len >> 16); //data len
        output[offset++] = (uint8_t)(body_len >> 8); //data len
        output[offset++] = (uint8_t)(body_len); //data len
        output[offset++] = (uint8_t)(ts >> 16); //time stamp
        output[offset++] = (uint8_t)(ts >> 8); //time stamp
        output[offset++] = (uint8_t)(ts); //time stamp
        output[offset++] = (uint8_t)(ts >> 24); //time stamp
        output[offset++] = in->pts; //stream id 0
        output[offset++] = 0x00; //stream id 0
        output[offset++] = 0x00; //stream id 0

        //FLV VideoTagHeader
        if(nal[startcode_len] == 0x065) {
            output[offset++] = 0x17; //key frame, AVC
            log_print(TAG, "Key frame \n");
        }
        else
            output[offset++] = 0x27; //not key frame, AVC
        output[offset++] = 0x01; //avc sequence header
        output[offset++] = 0x00; //composit time
        output[offset++] = 0x00; // composit time
        output[offset++] = 0x00; //composit time

        output[offset++] = (uint8_t)(nal_len >> 24); //nal length
        output[offset++] = (uint8_t)(nal_len >> 16); //nal length
        output[offset++] = (uint8_t)(nal_len >> 8); //nal length
        output[offset++] = (uint8_t)(nal_len); //nal length
        memcpy(output + offset, nal + startcode_len, nal_len);

        offset += nal_len;
        uint32_t fff = body_len + FLV_TAG_HEAD_LEN;
        output[offset++] = (uint8_t)(fff >> 24); //data len
        output[offset++] = (uint8_t)(fff >> 16); //data len
        output[offset++] = (uint8_t)(fff >> 8); //data len
        output[offset++] = (uint8_t)(fff); //data len

        frame_len = output_len;
        frame_buf = output;
#if 0
        out->size = output_len;
        out->data = (uint8_t *)malloc(output_len);
        memcpy(out->data, output, output_len);
        free(output);
#endif
    }

end:
    out->pts = in->pts;
    out->dts = in->dts;
    out->size = (uint32_t)(frame_len + sps_len);
    out->data = (uint8_t *)malloc(out->size);
    if (!out->data) {
        return -1;
    }

    if (sps_len > 0) {
        memcpy(out->data, sps_buf, sps_len);
        free(sps_buf);
    }
    if (frame_len > 0) {
        memcpy(out->data + sps_len, frame_buf, frame_len);
        free(frame_buf);
    }
    log_print(TAG, "sps:%d frame:%d %u out sp:%p\n", sps_len, frame_len, out->size, out->data);
    return (int)out->size;
}

int flvmux_close(struct flvmux_context *handle)
{
    if (handle) {
        free(handle);
    }
    return 0;
}

