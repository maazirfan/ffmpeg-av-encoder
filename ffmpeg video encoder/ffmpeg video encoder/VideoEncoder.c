//
//  VideoEncoder.c
//  ffmpeg video encoder
//
//  Created by Niall Mc Shane on 01/05/2014.
//  Copyright (c) 2014 Niall Mc Shane. All rights reserved.
//

#include <stdio.h>

//============================================================================
// Name        : example.cpp
// Author      : ocrespo
// Version     :
// Copyright   : Your copyright notice
// Description : Hello World in C, Ansi-style
//============================================================================

#include <stdio.h>
//#include <stdlib.h>
#include <dirent.h>


#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"
#include "libswscale/swscale.h"
#include "libswresample/swresample.h"
//#include "libswscale/version.h"

#include "libavutil/pixfmt.h"
#include "libavutil/opt.h"
//#include "libavutil/channel_layout.h"



#include <wchar.h>

//#include <jni.h>

//#include <android/log.h>

//#define LOG_TAG "VideoEncoder"
//#define LOGI(...) __android_log_print(4, LOG_TAG, __VA_ARGS__);
//#define LOGE(...) __android_log_print(6, LOG_TAG, __VA_ARGS__);



#define STREAM_FRAME_RATE 25 /* 25 images/s */
#define STREAM_PIX_FMT AV_PIX_FMT_YUV420P /* default pix_fmt */

static uint8_t **src_samples_data;
static int max_dst_nb_samples;
static int src_nb_samples;
static int src_samples_linesize;

uint8_t **dst_samples_data;
int dst_samples_linesize;
int dst_samples_size;
int samples_count;

static float t, tincr, tincr2;

AVFrame *audio_frame;
struct SwrContext *swr_ctx = NULL;

/***********************************************************************************************************************************************
 * **********GENERAL****************************************************************************************************************************
 * *********************************************************************************************************************************************
 */


int dir_cmp(const struct dirent  **first, const struct dirent **second){
	//PATH_MAX
	char file_1[256];
	char file_2[256];
	char *temp1 = file_1;
	char *temp2 = (char *)(*first)->d_name;
    
	// convert to upper case for comparison
	while (*temp2 != '\0'){
        *temp1++ = toupper((int)*(temp2++));
	}
    
	temp1 = file_2;
	temp2 = (char *)(*second)->d_name;
    
	while (*temp2 != '\0'){
        *temp1++ = toupper((int)*(temp2++));
	}
    
	return strcmp(file_1, file_2);
}
int dir_select(const   struct   dirent   *dir){
	if (strcmp(".", dir->d_name) == 0 || strcmp("..", dir->d_name) == 0){
        return 0;
	}
	else{
        return 1;
    }
}

uint8_t* readBytesFromFile(char* path){
	FILE *img = fopen(path,"rb");
	//LOGE( "filename %s\r\n", path);
    
	// obtain file size:
	fseek (img , 0 , SEEK_END);
	int lSize = ftell (img);
	rewind (img);
    
	// allocate memory to contain the whole file:
	uint8_t *inbuffer = (uint8_t*) malloc (sizeof(uint8_t)* lSize);
	if (inbuffer == NULL) {
		fputs ("Memory error",stderr);
		exit (2);
	}
    
	// copy the file into the buffer:
	int result = fread (inbuffer,1,lSize,img);
	if (result != lSize) {
		fputs ("Reading error",stderr);
		exit (3);
	}
	fclose(img);
	return inbuffer;
}

AVStream* add_stream(AVFormatContext *formatContext, AVCodec **codec,enum AVCodecID codec_id,int bit_rate,int out_width,int out_height){
    
	*codec = avcodec_find_encoder(codec_id);
    
    
	if (!(*codec)) {
		//LOGE("Encode doesn't found \n");
		exit(-2);
	}
    
	AVStream *stream = avformat_new_stream(formatContext, *codec);
	if (!stream) {
		//LOGE("Could not allocate stream\n");
		exit(1);
	}
	stream->id = formatContext->nb_streams-1;
	AVCodecContext *c = stream->codec;
    
	switch ((*codec)->type) {
		case AVMEDIA_TYPE_AUDIO:
			c->sample_fmt = (*codec)->sample_fmts ?
			(*codec)->sample_fmts[0] : AV_SAMPLE_FMT_FLTP;
			c->bit_rate = 64000;
			c->sample_rate = 44100;
			c->channels = 2;
			break;
		case AVMEDIA_TYPE_VIDEO:
			c->codec_id = codec_id;
			//c->bit_rate = 400000;
			c->bit_rate = bit_rate;
			/* Resolution must be a multiple of two. */
			c->width = out_width;
			c->height = out_height;
			/* timebase: This is the fundamental unit of time (in seconds) in terms
             * of which frame timestamps are represented. For fixed-fps content,
             * timebase should be 1/framerate and timestamp increments should be
             * identical to 1. */
			c->time_base.den = STREAM_FRAME_RATE;
			c->time_base.num = 1;
			c->gop_size = 12; /* emit one intra frame every twelve frames at most */
			c->pix_fmt = STREAM_PIX_FMT;
			if (c->codec_id == AV_CODEC_ID_MPEG2VIDEO) {
				/* just for testing, we also add B frames */
				c->max_b_frames = 2;
			}
			if (c->codec_id == AV_CODEC_ID_MPEG1VIDEO) {
				/* Needed to avoid using macroblocks in which some coeffs overflow.
                 * This does not happen with normal video, it just happens here as
                 * the motion of the chroma plane does not match the luma plane. */
				c->mb_decision = 2;
			}
			break;
		default:
			break;
	}
    
    /* Some formats want stream headers to be separate. */
	if (formatContext->oformat->flags & AVFMT_GLOBALHEADER){
		c->flags |= CODEC_FLAG_GLOBAL_HEADER;
	}
    
	return stream;
}


int write_frame(AVFormatContext *fmt_ctx, const AVRational *time_base, AVStream *st, AVPacket *pkt){
    
	/* rescale output packet timestamp values from codec to stream timebase */
	pkt->pts = av_rescale_q_rnd(pkt->pts, *time_base, st->time_base, AV_ROUND_NEAR_INF|AV_ROUND_PASS_MINMAX);
	pkt->dts = av_rescale_q_rnd(pkt->dts, *time_base, st->time_base, AV_ROUND_NEAR_INF|AV_ROUND_PASS_MINMAX);
	pkt->duration = av_rescale_q(pkt->duration, *time_base, st->time_base);
	pkt->stream_index = st->index;
	/* Write the compressed frame to the media file. */
	//log_packet(fmt_ctx, pkt);
    
	return av_interleaved_write_frame(fmt_ctx, pkt);
}

/***********************************************************************************************************************************************
 * **********VIDEO****************************************************************************************************************************
 * *********************************************************************************************************************************************
 */

void open_video(AVFormatContext *oc, AVCodec *codec, AVStream *st){
	int ret;
	AVCodecContext *c = st->codec;
    
	ret = avcodec_open2(c, codec, NULL);
	if (ret < 0) {
		//LOGE("Could not open video codec: %s\n", av_err2str(ret));
		exit(1);
	}
}


AVFrame* conver_image_to_encode(AVCodecContext* codec,uint8_t* inbuffer,int in_width,int in_height,int out_width,int out_height,struct SwsContext* sws_context){
	//int in_width, in_height, out_width, out_height;
    
	//here, make sure inbuffer points to the input BGR32 data,
	//and the input and output dimensions are set correctly.
    
	//calculate the bytes needed for the output image
	int nbytes = avpicture_get_size(codec->pix_fmt, out_width, out_height);
    
	//create ffmpeg frame structures.  These do not allocate space for image data,
	//just the pointers and other information about the image.
	AVFrame* inpic = av_frame_alloc();
	AVFrame* outpic = av_frame_alloc();
    
	//this will set the pointers in the frame structures to the right points in
	//the input and output buffers.
	avpicture_alloc((AVPicture*)inpic, AV_PIX_FMT_BGR24, in_width, in_height);
    
	int i;
	int j=0;
	for (i = in_height-1; i >= 0; i--){
		memcpy(&inpic->data[0][j*in_width * 3],&inbuffer[i*in_width * 3],in_width * 3);
		j++;
	}
    
	//create buffer for the output image
	uint8_t* outbuffer = (uint8_t*)av_malloc(nbytes*sizeof(uint8_t));
    
	avpicture_fill((AVPicture*)outpic, outbuffer, codec->pix_fmt, out_width, out_height);
    
    
	//return outpic;
    
	//create the conversion context
	//struct SwsContext* context = sws_getContext(in_width, in_height, PIX_FMT_BGR24, out_width, out_height, codec->pix_fmt, SWS_BICUBIC, NULL, NULL, NULL);
    
	//perform the conversion
	sws_scale(sws_context, (const uint8_t * const *)inpic->data, inpic->linesize, 0, in_height, outpic->data, outpic->linesize);
    
	av_free(inpic);
    
    
    
	return outpic;
}


void write_video_frame(char* path,AVStream *video_st,AVFormatContext *formatContext,int in_width,int in_height,int out_width,int out_height,struct SwsContext* sws_context){
	AVPacket pkt;
    
	av_init_packet(&pkt);
    
	pkt.data = NULL; // packet data will be allocated by the encoder
	pkt.size = 0;
    
	uint8_t* inbuffer = readBytesFromFile(path);
    
	AVFrame* frame = conver_image_to_encode(video_st->codec,inbuffer,in_width,in_height,out_width,out_height,sws_context);
    
	int got_output;
    
	int ret = avcodec_encode_video2(video_st->codec, &pkt, frame, &got_output);
	if (ret < 0) {
		//LOGE("Error encoding frame\n");
		exit(1);
	}
	if (got_output) {
		//LOGE("Write frame  (size=%5d)\n", pkt.size);
		write_frame(formatContext, &video_st->codec->time_base, video_st, &pkt);
	}
    
	av_free(frame);
	free(inbuffer);
}

void close_video(AVFormatContext *oc, AVStream *st){
    
	avcodec_close(st->codec);
    
}

/***********************************************************************************************************************************************
 * **********AUDIO****************************************************************************************************************************
 * *********************************************************************************************************************************************
 */

void open_audio(AVFormatContext *oc, AVCodec *codec, AVStream *st)
{
	AVCodecContext *c;
	int ret;
	c = st->codec;
	/* allocate and init a re-usable frame */
	audio_frame = av_frame_alloc();
	if (!audio_frame) {
		//LOGE("Could not allocate audio frame\n");
		exit(1);
	}
	/* open it */
	ret = avcodec_open2(c, codec, NULL);
	if (ret < 0) {
		//LOGE( "Could not open audio codec: %s\n", av_err2str(ret));
		exit(1);
	}
	/* init signal generator */
	t = 0;
	tincr = 2 * M_PI * 110.0 / c->sample_rate;
	/* increment frequency by 110 Hz per second */
	tincr2 = 2 * M_PI * 110.0 / c->sample_rate / c->sample_rate;
	src_nb_samples = c->codec->capabilities & CODEC_CAP_VARIABLE_FRAME_SIZE ?
	10000 : c->frame_size;
	ret = av_samples_alloc_array_and_samples(&src_samples_data, &src_samples_linesize, c->channels,
                                             src_nb_samples, AV_SAMPLE_FMT_S16, 0);
	if (ret < 0) {
		//LOGE("Could not allocate source samples\n");
		exit(1);
	}
	/* compute the number of converted samples: buffering is avoided
     * ensuring that the output buffer will contain at least all the
     * converted input samples */
	max_dst_nb_samples = src_nb_samples;
	/* create resampler context */
	if (c->sample_fmt != AV_SAMPLE_FMT_S16) {
		swr_ctx = swr_alloc();
		if (!swr_ctx) {
			//LOGE("Could not allocate resampler context\n");
			exit(1);
		}
		/* set options */
		av_opt_set_int (swr_ctx, "in_channel_count", c->channels, 0);
		av_opt_set_int (swr_ctx, "in_sample_rate", c->sample_rate, 0);
		av_opt_set_sample_fmt(swr_ctx, "in_sample_fmt", AV_SAMPLE_FMT_S16, 0);
		av_opt_set_int (swr_ctx, "out_channel_count", c->channels, 0);
		av_opt_set_int (swr_ctx, "out_sample_rate", c->sample_rate, 0);
		av_opt_set_sample_fmt(swr_ctx, "out_sample_fmt", c->sample_fmt, 0);
		/* initialize the resampling context */
		if ((ret = swr_init(swr_ctx)) < 0) {
			//LOGE("Failed to initialize the resampling context\n");
			exit(1);
		}
		ret = av_samples_alloc_array_and_samples(&dst_samples_data, &dst_samples_linesize, c->channels,
                                                 max_dst_nb_samples, c->sample_fmt, 0);
		if (ret < 0) {
			//LOGE("Could not allocate destination samples\n");
			exit(1);
		}
	}
	else {
		dst_samples_data = src_samples_data;
	}
    
	dst_samples_size = av_samples_get_buffer_size(NULL, c->channels, max_dst_nb_samples,
                                                  c->sample_fmt, 0);
}



/* Prepare a 16 bit dummy audio frame of 'frame_size' samples and
 * 'nb_channels' channels. */
static void get_audio_frame(int16_t *samples, int frame_size, int nb_channels){
    
	int j, i, v;
	int16_t *q;
	q = samples;
	for (j = 0; j < frame_size; j++) {
		v = (int)(sin(t) * 10000);
		for (i = 0; i < nb_channels; i++){
			*q++ = v;
		}
        
		t += tincr;
		tincr += tincr2;
	}
}

void write_audio_frame(AVFormatContext *oc, AVStream *st, int flush){
    
	AVCodecContext *c;
	AVPacket pkt = { 0 }; // data and size must be 0;
	int got_packet, ret, dst_nb_samples;
	av_init_packet(&pkt);
	c = st->codec;
	if (!flush) {
		get_audio_frame((int16_t *)src_samples_data[0], src_nb_samples, c->channels);
		/* convert samples from native format to destination codec format, using the resampler */
		if (swr_ctx) {
			/* compute destination number of samples */
			dst_nb_samples = av_rescale_rnd(swr_get_delay(swr_ctx, c->sample_rate) + src_nb_samples,
                                            c->sample_rate, c->sample_rate, AV_ROUND_UP);
			if (dst_nb_samples > max_dst_nb_samples) {
                av_free(dst_samples_data[0]);
                ret = av_samples_alloc(dst_samples_data, &dst_samples_linesize, c->channels,
                                       dst_nb_samples, c->sample_fmt, 0);
                if (ret < 0)
                    exit(1);
                max_dst_nb_samples = dst_nb_samples;
                dst_samples_size = av_samples_get_buffer_size(NULL, c->channels, dst_nb_samples,
                                                              c->sample_fmt, 0);
            }
            
            /* convert to destination format */
            ret = swr_convert(swr_ctx,
                              dst_samples_data, dst_nb_samples,
                              (const uint8_t **)src_samples_data, src_nb_samples);
            
            if (ret < 0) {
                //LOGE("Error while converting\n");
                exit(1);
            }
        }
		else {
			dst_nb_samples = src_nb_samples;
        }
        
        audio_frame->nb_samples = dst_nb_samples;
        audio_frame->pts = av_rescale_q(samples_count, (AVRational){1, c->sample_rate}, c->time_base);
        avcodec_fill_audio_frame(audio_frame, c->channels, c->sample_fmt,
                                 dst_samples_data[0], dst_samples_size, 0);
        samples_count += dst_nb_samples;
	}
	ret = avcodec_encode_audio2(c, &pkt, flush ? NULL : audio_frame, &got_packet);
	if (ret < 0) {
		//LOGE("Error encoding audio frame: %s\n", av_err2str(ret));
		exit(1);
	}
	if (!got_packet) {
		//if (flush)
        //audio_is_eof = 1;
		return;
	}
	ret = write_frame(oc, &c->time_base, st, &pkt);
	if (ret < 0) {
		//LOGE("Error while writing audio frame: %s\n",av_err2str(ret));
		exit(1);
	}
}

void close_audio(AVFormatContext *oc, AVStream *st){
    
	avcodec_close(st->codec);
    
	if (dst_samples_data != src_samples_data) {
		av_free(dst_samples_data[0]);
		av_free(dst_samples_data);
	}
    
	av_free(src_samples_data[0]);
	av_free(src_samples_data);
	av_frame_free(&audio_frame);
}



/***********************************************************************************************************************************************
 * **********MAIN****************************************************************************************************************************
 * *********************************************************************************************************************************************
 */

int create_video_from_directory(char* path,char* out_file,int in_width,int in_height,int bit_rate,int out_width,int out_height){
	struct dirent *pDirent;
	struct  dirent  **pDirs;
    
	AVCodec *audio_codec, *video_codec;
    
	av_register_all();
    
	AVFormatContext *formatContext;
	avformat_alloc_output_context2(&formatContext, NULL, NULL, out_file);
	if (!formatContext) {
		//LOGE("Could not deduce output format from file extension: using MPEG.\n");
		avformat_alloc_output_context2(&formatContext, NULL, "mpeg", out_file);
	}
	if (!formatContext)
		return 1;
    
	AVStream *audio_st, *video_st;
    
	AVOutputFormat *format = formatContext->oformat;
	/* Add the audio and video streams using the default format codecs
     * and initialize the codecs. */
	video_st = NULL;
	audio_st = NULL;
	if (format->video_codec != AV_CODEC_ID_NONE){
		video_st = add_stream(formatContext, &video_codec, format->video_codec,bit_rate,out_width,out_height);
	}
    
	if (format->audio_codec != AV_CODEC_ID_NONE){
		audio_st = add_stream(formatContext, &audio_codec, format->audio_codec,bit_rate,out_width,out_height);
	}
    
    /* Now that all the parameters are set, we can open the audio and
     * video codecs and allocate the necessary encode buffers. */
	if (video_st){
		open_video(formatContext, video_codec, video_st);
	}
    
	if (audio_st){
		open_audio(formatContext, audio_codec, audio_st);
	}
    
    
	//int out_width, out_height;
    
	int ret;
	if (!(format->flags & AVFMT_NOFILE)) {
		ret = avio_open(&formatContext->pb, out_file, AVIO_FLAG_WRITE);
		if (ret < 0) {
			//LOGE("Could not open '%s': %s\n", out_file,av_err2str(ret));
			return 1;
		}
	}
    
	ret = avformat_write_header(formatContext, NULL);
	if (ret < 0) {
		//LOGE("Error occurred when opening output file: %s\n",av_err2str(ret));
		return 1;
	}
	//out_width=video_st->codec->width;
	//out_height=video_st->codec->height;
    
	char* aux_path = (char*)malloc(256*sizeof(char));
	int i = 0;
    
	int dirNum = scandir(path,&pDirs,dir_select,dir_cmp);
	if(dirNum <= 0){
		//LOGE( "could not open %s\r\n", path);
		return -1;
	}
    
	struct SwsContext* sws_context = sws_getContext(in_width, in_height, AV_PIX_FMT_BGR24, out_width, out_height,video_st->codec->pix_fmt, SWS_BICUBIC, NULL, NULL, NULL);
    
	double audio_time, video_time;
	while(dirNum > 0){
        /* Compute current audio and video time. */
		audio_time = (audio_st) ? audio_st->pts.val * av_q2d(audio_st->time_base) : INFINITY;
		video_time = (video_st) ? video_st->pts.val * av_q2d(video_st->time_base) : INFINITY;
        
        /* write interleaved audio and video frames */
		if (audio_st && audio_time <= video_time) {
			write_audio_frame(formatContext, audio_st,0);
		}
		else if (video_st && video_time < audio_time) {
			pDirent = pDirs[i];
			sprintf(aux_path,"%s/%s",path,pDirent->d_name);
            
			write_video_frame(aux_path,video_st,formatContext,in_width,in_height,out_width,out_height,sws_context);
            
			i++;
			dirNum--;
            
		}
		if(i == 100){
			//LOGE("500\n");
		}
        
	}
	//LOGE("END ENCODE\n");
    
	sws_freeContext(sws_context);
	//closedir(pDirs);
    av_write_trailer(formatContext);
	/* Close each codec. */
	if (video_st){
		close_video(formatContext, video_st);
	}
    
	if (audio_st){
		close_audio(formatContext, audio_st);
	}
    
	if (!(format->flags & AVFMT_NOFILE)){
		/* Close the output file. */
		avio_close(formatContext->pb);
	}
    
	// free the stream
	avformat_free_context(formatContext);
    
	//LOGE("END \r\n");
    
	return 0;
}

int generate_video_from_images(char* dir,char* out_file,int in_width,int in_height,int bit_rate,int out_width,int out_height) {
	return create_video_from_directory(dir,out_file,in_width,in_height,bit_rate,out_width,out_height);
	//return EXIT_SUCCESS;
}