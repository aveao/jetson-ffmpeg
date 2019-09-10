
#include "nvmpi.h"
#include "NvVideoDecoder.h"
#include "nvbuf_utils.h"
#include <vector>
#include <iostream>
#include <thread>
#include <unistd.h>

#define CHUNK_SIZE 4000000
#define MAX_BUFFERS 32

#define TEST_ERROR(condition, message, errorCode)    \
	if (condition)                               \
{                                                    \
	std::cout<< message;			     \
}

using namespace std;

struct nvmpictx
{
	NvVideoDecoder *dec;
	bool eos=false;
	int picture_index;
	int index;
	unsigned int coded_width;
	unsigned int coded_height;
	int dst_dma_fd;
	int numberCaptureBuffers;	
	int dmaBufferFileDescriptor[MAX_BUFFERS];
	nvPixFormat out_pixfmt;
	std::thread * dec_capture_loop;
	unsigned char * bufptr_0[MAX_BUFFERS];
	unsigned char * bufptr_1[MAX_BUFFERS];
	unsigned char * bufptr_2[MAX_BUFFERS];
	unsigned int frame_size[3];
	unsigned int frame_pitch[3];
	unsigned long long timestamp[MAX_BUFFERS];
};

void respondToResolutionEvent(v4l2_format &format, v4l2_crop &crop,nvmpictx* ctx){
	int32_t minimumDecoderCaptureBuffers;
	int ret=0;
	NvBufferCreateParams input_params = {0};
	NvBufferCreateParams cParams = {0};

	ret = ctx->dec->capture_plane.getFormat(format);	
	TEST_ERROR(ret < 0, "Error: Could not get format from decoder capture plane", ret);

	ret = ctx->dec->capture_plane.getCrop(crop);
	TEST_ERROR(ret < 0, "Error: Could not get crop from decoder capture plane", ret);

	ctx->coded_width=crop.c.width;
	ctx->coded_height=crop.c.height;

	//printf("coded_width:%d\n",ctx->coded_width);

	if(ctx->dst_dma_fd != -1)
	{
		NvBufferDestroy(ctx->dst_dma_fd);
		ctx->dst_dma_fd = -1;
	}

	input_params.payloadType = NvBufferPayload_SurfArray;
	input_params.width = crop.c.width;
	input_params.height = crop.c.height;
	input_params.layout = NvBufferLayout_Pitch;
	input_params.colorFormat = ctx->out_pixfmt==NV_PIX_NV12?NvBufferColorFormat_NV12: NvBufferColorFormat_YUV420;
	input_params.nvbuf_tag = NvBufferTag_VIDEO_DEC;

	ctx->dec->capture_plane.deinitPlane();

	for (int index = 0; index < ctx->numberCaptureBuffers; index++)
	{
		if (ctx->dmaBufferFileDescriptor[index] != 0)
		{	//printf("dmaBufferFileDescriptor[index]:%d\n",ctx->dmaBufferFileDescriptor[index]);
			ret = NvBufferDestroy(ctx->dmaBufferFileDescriptor[index]);
			TEST_ERROR(ret < 0, "Failed to Destroy NvBuffer", ret);
		}

	}


	ret=ctx->dec->setCapturePlaneFormat(format.fmt.pix_mp.pixelformat,format.fmt.pix_mp.width,format.fmt.pix_mp.height);
	TEST_ERROR(ret < 0, "Error in setting decoder capture plane format", ret);

	ctx->dec->getMinimumCapturePlaneBuffers(minimumDecoderCaptureBuffers);
	TEST_ERROR(ret < 0, "Error while getting value of minimum capture plane buffers",ret);

	ctx->numberCaptureBuffers = minimumDecoderCaptureBuffers + 5;

	//printf("numberCaptureBuffers:%d\n",ctx->numberCaptureBuffers );


	switch (format.fmt.pix_mp.colorspace)
	{
		case V4L2_COLORSPACE_SMPTE170M:
			if (format.fmt.pix_mp.quantization == V4L2_QUANTIZATION_DEFAULT)
			{

				//cParams.colorFormat=NVBUF_COLOR_FORMAT_NV12;
				//LOG << "Decoder colorspace ITU-R BT.601 with standard range luma (16-235)";
				cParams.colorFormat = NvBufferColorFormat_NV12;
			}
			else
			{
				//cParams.colorFormat=NVBUF_COLOR_FORMAT_NV12_ER;
				//LOG << "Decoder colorspace ITU-R BT.601 with extended range luma (0-255)";
				cParams.colorFormat = NvBufferColorFormat_NV12_ER;
			}
			break;
		case V4L2_COLORSPACE_REC709:
			if (format.fmt.pix_mp.quantization == V4L2_QUANTIZATION_DEFAULT)
			{
				//cParams.colorFormat=NVBUF_COLOR_FORMAT_NV12_709;
				//LOG << "Decoder colorspace ITU-R BT.709 with standard range luma (16-235)";
				cParams.colorFormat = NvBufferColorFormat_NV12_709;
			}
			else
			{
				//cParams.colorFormat=NVBUF_COLOR_FORMAT_NV12_709_ER;
				//LOG << "Decoder colorspace ITU-R BT.709 with extended range luma (0-255)";
				cParams.colorFormat = NvBufferColorFormat_NV12_709_ER;
			}
			break;
		case V4L2_COLORSPACE_BT2020:
			{
				//cParams.colorFormat=NVBUF_COLOR_FORMAT_NV12_2020;
				//LOG << "Decoder colorspace ITU-R BT.2020";
				cParams.colorFormat = NvBufferColorFormat_NV12_2020;
			}
			break;
		default:
			//LOG << "supported colorspace details not available, use default";
			if (format.fmt.pix_mp.quantization == V4L2_QUANTIZATION_DEFAULT)
			{
				//cParams.colorFormat=NVBUF_COLOR_FORMAT_NV12;
				//LOG << "Decoder colorspace ITU-R BT.601 with standard range luma (16-235)";
				cParams.colorFormat = NvBufferColorFormat_NV12;
			}
			else
			{
				//cParams.colorFormat=NVBUF_COLOR_FORMAT_NV12_ER;
				//LOG << "Decoder colorspace ITU-R BT.601 with extended range luma (0-255)";
				cParams.colorFormat = NvBufferColorFormat_NV12_ER;
			}
			break;
	}



	ret = NvBufferCreateEx (&ctx->dst_dma_fd, &input_params);
	TEST_ERROR(ret == -1, "create dst_dmabuf failed", error);

	for (int index = 0; index < ctx->numberCaptureBuffers; index++)
	{
		cParams.width = crop.c.width;
		cParams.height = crop.c.height;
		cParams.layout = NvBufferLayout_BlockLinear;
		cParams.payloadType = NvBufferPayload_SurfArray;
		cParams.nvbuf_tag = NvBufferTag_VIDEO_DEC;

		ret = NvBufferCreateEx(&ctx->dmaBufferFileDescriptor[index], &cParams);
		TEST_ERROR(ret < 0, "Failed to create buffers", ret);

	}	

	ctx->dec->capture_plane.reqbufs(V4L2_MEMORY_DMABUF, ctx->numberCaptureBuffers);
	TEST_ERROR(ret < 0, "Error in decoder capture plane streamon", ret);

	ctx->dec->capture_plane.setStreamStatus(true);
	TEST_ERROR(ret < 0, "Error in decoder capture plane streamon", ret);


	for (uint32_t i = 0; i < ctx->dec->capture_plane.getNumBuffers(); i++)
	{
		struct v4l2_buffer v4l2_buf;
		struct v4l2_plane planes[MAX_PLANES];

		memset(&v4l2_buf, 0, sizeof(v4l2_buf));
		memset(planes, 0, sizeof(planes));

		v4l2_buf.index = i;
		v4l2_buf.m.planes = planes;
		v4l2_buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		v4l2_buf.memory = V4L2_MEMORY_DMABUF;
		v4l2_buf.m.planes[0].m.fd = ctx->dmaBufferFileDescriptor[i];
		ret = ctx->dec->capture_plane.qBuffer(v4l2_buf, NULL);
		TEST_ERROR(ret < 0, "Error Qing buffer at output plane", ret);
	}

	printf("respondToResolutionEvent done\n");



}

void *dec_capture_loop_fcn(void *arg){
	nvmpictx* ctx=(nvmpictx*)arg;

	struct v4l2_format v4l2Format;
	struct v4l2_crop v4l2Crop;
	struct v4l2_event v4l2Event;
	int ret;
	do{
		ret = ctx->dec->dqEvent(v4l2Event, 50000);
		if (ret < 0){
			if (errno == EAGAIN){
				printf( "Timed out waiting for first V4L2_EVENT_RESOLUTION_CHANGE\n");
			}else{

				printf("Error in dequeueing decoder event\n");
			}

			break;
		}
	}while((v4l2Event.type != V4L2_EVENT_RESOLUTION_CHANGE));

	respondToResolutionEvent(v4l2Format, v4l2Crop, ctx);

	while (!ctx->dec->isInError()){
		NvBuffer *dec_buffer;
		ret = ctx->dec->dqEvent(v4l2Event, false);	
		if (ret == 0)
		{
			switch (v4l2Event.type)
			{
				case V4L2_EVENT_RESOLUTION_CHANGE:
					respondToResolutionEvent(v4l2Format, v4l2Crop,ctx);
					continue;
			}
		}	



		while(true){
			struct v4l2_buffer v4l2_buf;
			struct v4l2_plane planes[MAX_PLANES];
			v4l2_buf.m.planes = planes;
			if (ctx->dec->capture_plane.dqBuffer(v4l2_buf, &dec_buffer, NULL, 0)){
				if (errno == EAGAIN)
				{
					usleep(1000);
				}
				else
				{

					printf( "Error while calling dequeue at capture plane\n");
					ctx->eos=true;
				}
				break;

			}


			dec_buffer->planes[0].fd = ctx->dmaBufferFileDescriptor[v4l2_buf.index];
			NvBufferRect src_rect, dest_rect;
			src_rect.top = 0;
			src_rect.left = 0;
			src_rect.width = ctx->coded_width;
			src_rect.height = ctx->coded_height;
			dest_rect.top = 0;
			dest_rect.left = 0;
			dest_rect.width = ctx->coded_width;
			dest_rect.height = ctx->coded_height;

			NvBufferTransformParams transform_params;
			memset(&transform_params,0,sizeof(transform_params));
			transform_params.transform_flag = NVBUFFER_TRANSFORM_FILTER;
			transform_params.transform_flip = NvBufferTransform_None;
			transform_params.transform_filter = NvBufferTransform_Filter_Smart;
			transform_params.src_rect = src_rect;
			transform_params.dst_rect = dest_rect;

			ret = NvBufferTransform(dec_buffer->planes[0].fd, ctx->dst_dma_fd, &transform_params);
			TEST_ERROR(ret==-1, "Transform failed",ret);

			NvBufferParams parm;
			ret = NvBufferGetParams(ctx->dst_dma_fd, &parm);

			//			printf("nv_buffer_size:%u\n",parm.nv_buffer_size);
			//			printf("pixel_format:%d\n",parm.pixel_format);
			//			printf("parm.pitch[0]:%u [1]:%u [2]:%u\n",parm.pitch[0],parm.pitch[1],parm.pitch[2]);
			//			printf("parm.offset[0]:%u [1]:%u [2]:%u\n",parm.offset[0],parm.offset[1],parm.offset[2]);
			//			printf("parm.width[0]:%u [1]:%u [2]:%u\n",parm.width[0],parm.width[1],parm.width[2]);
			//			printf("parm.height[0]:%u [1]:%u [2]:%u\n",parm.height[0],parm.height[1],parm.height[2]);
			//			printf("parm.psize[0]:%u [1]:%u [2]:%u\n",parm.psize[0],parm.psize[1],parm.psize[2]);
			//			printf("parm.layout[0]:%u [1]:%u [2]:%u\n",parm.layout[0],parm.layout[1],parm.layout[2]);
			//			printf("\n");

			if(!ctx->frame_size[0]){

				for(int index=0;index<ctx->numberCaptureBuffers;index++){
					ctx->bufptr_0[index]=new unsigned char[parm.psize[0]];//Y
					ctx->bufptr_1[index]=new unsigned char[parm.psize[1]];//UV or UU
					ctx->bufptr_2[index]=new unsigned char[parm.psize[2]];//VV

				}

			}

			ctx->frame_pitch[0]=parm.width[0];
			ctx->frame_pitch[1]=parm.width[1];
			ctx->frame_pitch[2]=parm.width[2];

			ctx->frame_size[0]=parm.psize[0];
			ctx->frame_size[1]=parm.psize[1];
			ctx->frame_size[2]=parm.psize[2];

			ret=NvBuffer2Raw(ctx->dst_dma_fd,0,parm.width[0],parm.height[0],ctx->bufptr_0[v4l2_buf.index]);
			ret=NvBuffer2Raw(ctx->dst_dma_fd,1,parm.width[1],parm.height[1],ctx->bufptr_1[v4l2_buf.index]);	
			ret=NvBuffer2Raw(ctx->dst_dma_fd,2,parm.width[2],parm.height[2],ctx->bufptr_2[v4l2_buf.index]);	

			ctx->picture_index=v4l2_buf.index;
			ctx->timestamp[v4l2_buf.index]=v4l2_buf.timestamp.tv_usec;

			v4l2_buf.m.planes[0].m.fd = ctx->dmaBufferFileDescriptor[v4l2_buf.index];
			if (ctx->dec->capture_plane.qBuffer(v4l2_buf, NULL) < 0){
				printf("Error while queueing buffer at decoder capture plane\n");

			}


		}

	}
}

nvmpictx* nvmpi_create_decoder(nvCodingType codingType,nvPixFormat pixFormat){

	int ret;

	nvmpictx* ctx=(nvmpictx*)malloc(sizeof(nvmpictx));

	ctx->dec = NvVideoDecoder::createVideoDecoder("dec0");
	TEST_ERROR(!ctx->dec, "Could not create decoder",ret);


	ret=ctx->dec->subscribeEvent(V4L2_EVENT_RESOLUTION_CHANGE, 0, 0);
	TEST_ERROR(ret < 0, "Could not subscribe to V4L2_EVENT_RESOLUTION_CHANGE", ret);

	if(codingType==NV_VIDEO_CodingH264){
		ret = ctx->dec->setOutputPlaneFormat(V4L2_PIX_FMT_H264, CHUNK_SIZE);
	}else if(codingType==NV_VIDEO_CodingHEVC){
		ret = ctx->dec->setOutputPlaneFormat(V4L2_PIX_FMT_H265, CHUNK_SIZE);
	}

	TEST_ERROR(ret < 0, "Could not set output plane format", ret);

	//ctx->nalu_parse_buffer = new char[CHUNK_SIZE];
	ret = ctx->dec->setFrameInputMode(0);
	TEST_ERROR(ret < 0, "Error in decoder setFrameInputMode for NALU", ret);

	ret = ctx->dec->output_plane.setupPlane(V4L2_MEMORY_USERPTR, 10, false, true);
	TEST_ERROR(ret < 0, "Error while setting up output plane", ret);

	ctx->dec->output_plane.setStreamStatus(true);
	TEST_ERROR(ret < 0, "Error in output plane stream on", ret);

	ctx->out_pixfmt=pixFormat;
	ctx->dst_dma_fd=-1;
	ctx->eos=false;
	ctx->index=0;
	ctx->picture_index=-1;
	ctx->frame_size[0]=0;

	for(int index=0;index<MAX_BUFFERS;index++)
		ctx->dmaBufferFileDescriptor[index]=0;
	ctx->numberCaptureBuffers=0;
	ctx->dec_capture_loop=new thread(dec_capture_loop_fcn,ctx);

	return ctx;
}




int nvmpi_decoder_put_packet(nvmpictx* ctx,nvPacket* packet){
	int ret;


	struct v4l2_buffer v4l2_buf;
	struct v4l2_plane planes[MAX_PLANES];
	NvBuffer *nvBuffer;

	memset(&v4l2_buf, 0, sizeof(v4l2_buf));
	memset(planes, 0, sizeof(planes));

	v4l2_buf.m.planes = planes;

	if (ctx->index < (int)ctx->dec->output_plane.getNumBuffers()) {
		nvBuffer = ctx->dec->output_plane.getNthBuffer(ctx->index);
	} else {
		ret = ctx->dec->output_plane.dqBuffer(v4l2_buf, &nvBuffer, NULL, -1);
		if (ret < 0) {
			cout << "Error DQing buffer at output plane" << std::endl;
			return false;
		}
	}

	memcpy(nvBuffer->planes[0].data,packet->payload,packet->payload_size);
	nvBuffer->planes[0].bytesused=packet->payload_size;



	if (ctx->index < ctx->dec->output_plane.getNumBuffers())
	{
		v4l2_buf.index = ctx->index ;
		v4l2_buf.m.planes = planes;
	}

	v4l2_buf.m.planes[0].bytesused = nvBuffer->planes[0].bytesused;

	v4l2_buf.flags |= V4L2_BUF_FLAG_TIMESTAMP_COPY;
	//v4l2_buf.timestamp.tv_sec = packet->pts /1000000;
	v4l2_buf.timestamp.tv_usec = packet->pts;// - (v4l2_buf.timestamp.tv_sec * (time_t)1000000);

	//printf("pts:%u sec:%u usec:%u\n",packet->pts,v4l2_buf.timestamp.tv_sec,v4l2_buf.timestamp.tv_usec);


	ret = ctx->dec->output_plane.qBuffer(v4l2_buf, NULL);
	if (ret < 0) {
		std::cout << "Error Qing buffer at output plane" << std::endl;
		return false;
	}

	if (ctx->index < ctx->dec->output_plane.getNumBuffers())
		ctx->index++;

	if (v4l2_buf.m.planes[0].bytesused == 0) {
		ctx->eos=true;
		std::cout << "Input file read complete" << std::endl;
	}


	return 0;

}


int nvmpi_decoder_get_frame(nvmpictx* ctx,nvFrame* frame){

	int ret;

	if(ctx->picture_index<0)
		return -1;

	frame->width=ctx->coded_width;
	frame->height=ctx->coded_height;


	frame->pitch[0]=ctx->frame_pitch[0];
	frame->pitch[1]=ctx->frame_pitch[1];
	frame->pitch[2]=ctx->frame_pitch[2];

	frame->payload[0]=ctx->bufptr_0[ctx->picture_index];
	frame->payload[1]=ctx->bufptr_1[ctx->picture_index];
	frame->payload[2]=ctx->bufptr_2[ctx->picture_index];

	frame->payload_size[0]=ctx->frame_size[0];
	frame->payload_size[1]=ctx->frame_size[1];
	frame->payload_size[2]=ctx->frame_size[2];
	frame->timestamp=ctx->timestamp[ctx->picture_index];
	ctx->picture_index=-1;


	return 0;

}

int nvmpi_decoder_close(nvmpictx* ctx){

	// The decoder destructor does all the cleanup i.e set streamoff on output and capture planes,
	// unmap buffers, tell decoder to deallocate buffer (reqbufs ioctl with counnt = 0),
	// and finally call v4l2_close on the fd.

	ctx->eos=true;
	delete ctx->dec;

	for(int index=0;index<ctx->numberCaptureBuffers;index++){
		delete ctx->bufptr_0[index];
		delete ctx->bufptr_1[index];
		delete ctx->bufptr_2[index];

	}


	if(ctx->dst_dma_fd != -1){
		NvBufferDestroy(ctx->dst_dma_fd);
		ctx->dst_dma_fd = -1;
	}

	free(ctx);


	return 0;
}

int nvmpi_decoder_flush(nvmpictx* ctx){
	printf("nvmpi_decoder_flush\n");


	return 0;
}
