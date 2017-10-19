// 摄像头采集.cpp: 定义控制台应用程序的入口点。
//
/******************************************************************************

Copyright (C),2007-2016, LonBon Technologies Co. Ltd. All Rights Reserved.

******************************************************************************
Version       : 1.1
Author        : 肖兴宇
Created       : 2017/10/19
Description   :（添加播放，添加dshow获取摄像头名回调）
History       :1.0 肖兴宇 2017/10/18（实现摄像头数据采集）
1.Date        :
Author        :
Modification  :
******************************************************************************/

#include "stdafx.h"
#include <stdio.h>  
#include <iostream>  

#define 严格
#define USE_DSHOW

#define __STDC_CONSTANT_MACROS  
#ifdef _WIN32  
//Windows  
extern "C"
{
#include "libavcodec/avcodec.h"  
#include "libavformat/avformat.h"  
#include "libswscale/swscale.h"  
#include "libavutil/imgutils.h"  
#include "libavdevice/avdevice.h"
#include "SDL2/SDL.h"  
};
#else  
//Linux...  
#ifdef __cplusplus  
extern "C"
{
#endif  
#include <libavcodec/avcodec.h>  
#include <libavformat/avformat.h>  
#include <libswscale/swscale.h>  
#include <libavutil/imgutils.h> 
#include "libavdevice/avdevice.h"
#include <SDL2/SDL.h>  
#ifdef __cplusplus  
};
#endif  
#endif  
//Refresh Event  
#define SFM_REFRESH_EVENT  (SDL_USEREVENT + 1)  

#define SFM_BREAK_EVENT  (SDL_USEREVENT + 2)  

int thread_exit = 0;
int thread_pause = 0;

int sfp_refresh_thread(void *opaque) {
	thread_exit = 0;
	thread_pause = 0;

	while (!thread_exit) {
		if (!thread_pause) {
			SDL_Event event;
			event.type = SFM_REFRESH_EVENT;
			SDL_PushEvent(&event);
		}
		SDL_Delay(40);
	}
	thread_exit = 0;
	thread_pause = 0;
	//Break  
	SDL_Event event;
	event.type = SFM_BREAK_EVENT;
	SDL_PushEvent(&event);

	return 0;
}
void logCallback(void*, int, const char*, va_list);
int main(int argc, char* argv[])
{
	AVFormatContext *pFormatCtx;
	av_register_all(); 
	avdevice_register_all();//注册所有设备 
	pFormatCtx = avformat_alloc_context();
#ifdef _WIN32
#ifdef USE_DSHOW
	AVInputFormat*ifmt = av_find_input_format("dshow");//使用dshow
	AVDictionary* options = NULL;
	av_dict_set(&options, "list_devices", "true", 0);
	AVInputFormat *iformat = av_find_input_format("dshow");
	av_log_set_callback(logCallback);
	avformat_open_input(&pFormatCtx, "video=dummy", iformat, &options);

	if (avformat_open_input(&pFormatCtx, "video=Vimicro USB Camera (Altair)", ifmt, NULL) < 0) {
		printf("Couldn't open input stream.（无法打开输入流）\n");
		return -1;
	}
#else
	AVInputFormat *ifmt = av_find_input_format("vfwcap");//使用vfwcap
	//AVDictionary* options = NULL;
	//av_dict_set(&options, "list_devices", "true", 0);
	if (avformat_open_input(&pFormatCtx, "0", ifmt, NULL) < 0) {
		printf("Couldn't open input stream.（无法打开输入流）\n");
		return -1;
	}
#endif
#else
#ifdef linux
	AVInputFormat *ifmt = av_find_input_format("video4linux2");//使用video4linux2
	if (avformat_open_input(&pFormatCtx, "/dev/video0", ifmt, NULL) != 0) {
		printf("Couldn't open input stream.（无法打开输入流）\n");
		return -1;
	}
#endif//linux
#endif // _WIN32
	if (avformat_find_stream_info(pFormatCtx, NULL)<0) {
		printf("Couldn't find stream information.\n");
		return -1;
	}
	auto videoindex = -1; 
	int i;
	for (i = 0; i < pFormatCtx->nb_streams; i++) {
		if (pFormatCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
			videoindex = i;
			break;
		}
	}
	if (videoindex == -1) {
		printf("Didn't find a video stream.\n");
		return -1;
	}
	AVCodec *codec = avcodec_find_decoder(pFormatCtx->streams[i]->codecpar->codec_id);

	auto pCodecCtx = avcodec_alloc_context3(codec);//pFormatCtx->streams[videoindex]->codec;
	avcodec_parameters_to_context(pCodecCtx, pFormatCtx->streams[videoindex]->codecpar);
	auto pCodec = avcodec_find_decoder(pCodecCtx->codec_id);
	if (pCodec == NULL) {
		printf("Codec not found.\n");
		return -1;
	}
	if (avcodec_open2(pCodecCtx, pCodec, NULL)<0) {
		printf("Could not open codec.\n");
		return -1;
	}
	auto pFrame = av_frame_alloc();
	auto pFrameYUV = av_frame_alloc();

	auto out_buffer = (unsigned char *)av_malloc(av_image_get_buffer_size(AV_PIX_FMT_YUV420P, pCodecCtx->width, pCodecCtx->height, 1));
	av_image_fill_arrays(pFrameYUV->data, pFrameYUV->linesize, out_buffer,
		AV_PIX_FMT_YUV420P, pCodecCtx->width, pCodecCtx->height, 1);

	//设置一些参数sws_scale会用到
	auto img_convert_ctx = sws_getContext(pCodecCtx->width, pCodecCtx->height, pCodecCtx->pix_fmt,
		pCodecCtx->width, pCodecCtx->height, AV_PIX_FMT_YUV420P, SWS_BICUBIC, NULL, NULL, NULL);


	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER)) {
		printf("Could not initialize SDL - %s\n", SDL_GetError());
		return -1;
	}
	//SDL 2.0 Support for multiple windows  
	auto screen_w = pCodecCtx->width;
	auto screen_h = pCodecCtx->height;
	auto screen = SDL_CreateWindow("摄像头采集与播放-来邦科技", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
		screen_w, screen_h, SDL_WINDOW_SHOWN);

	if (!screen) {
		printf("SDL: could not create window - exiting:%s\n", SDL_GetError());
		return -1;
	}
	auto sdlRenderer = SDL_CreateRenderer(screen, -1, 0);
	//IYUV: Y + U + V  (3 planes)  
	//YV12: Y + V + U  (3 planes)  
	auto sdlTexture = SDL_CreateTexture(sdlRenderer, SDL_PIXELFORMAT_IYUV, SDL_TEXTUREACCESS_STREAMING, pCodecCtx->width, pCodecCtx->height);


	auto packet = (AVPacket *)av_malloc(sizeof(AVPacket));
#ifdef 严格
	auto video_tid = SDL_CreateThread(sfp_refresh_thread, NULL, NULL);
#endif
	//------------SDL End------------  
	//Event Loop  

	while(true) {
		//Wait event
#ifdef 严格
		SDL_Event event;
		SDL_WaitEvent(&event);
		if (event.type == SFM_REFRESH_EVENT) {
#endif
			while (1) {
				if (av_read_frame(pFormatCtx, packet)<0)
					thread_exit = 1;

				if (packet->stream_index == videoindex)
					break;
			}
		auto 	ret = avcodec_send_packet(pCodecCtx, packet)&
				avcodec_receive_frame(pCodecCtx, pFrame);
			if (ret < 0) {
				printf("Decode Error.\n");
				return -1;
			}
			else {
				sws_scale(img_convert_ctx, (const unsigned char* const*)pFrame->data, pFrame->linesize, 0, pCodecCtx->height, pFrameYUV->data, pFrameYUV->linesize);//生成图片
																																									//SDL---------------------------  
				SDL_UpdateTexture(sdlTexture, NULL, pFrameYUV->data[0], pFrameYUV->linesize[0]);
				SDL_RenderClear(sdlRenderer);
				//SDL_RenderCopy( sdlRenderer, sdlTexture, &sdlRect, &sdlRect );    
				SDL_RenderCopy(sdlRenderer, sdlTexture, NULL, NULL);
				SDL_RenderPresent(sdlRenderer);
				//SDL End-----------------------  
			}
			av_packet_unref(packet);
#ifdef 严格

		}
		else if (event.type == SDL_KEYDOWN) {
			//Pause  
			if (event.key.keysym.sym == SDLK_SPACE)
				thread_pause = !thread_pause;
		}
		else if (event.type == SDL_QUIT) {
			thread_exit = 1;
		}
		else if (event.type == SFM_BREAK_EVENT) {
			break;
		}
#else
			SDL_Delay(40);
			if (thread_exit)break;

#endif
	}

		sws_freeContext(img_convert_ctx);

		SDL_Quit();
		//--------------  
		av_frame_free(&pFrameYUV);
		av_frame_free(&pFrame);
		avcodec_close(pCodecCtx);
		avformat_close_input(&pFormatCtx);

		return 0;
    return 0;
}

	void logCallback(void *, int, const char *, va_list)
	{
	}
