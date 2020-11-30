#include <iostream>
#include <memory>
#include <string>
#include "gb28181.grpc.pb.h"
#include "grpc/grpc.h"
#include "grpc++/channel.h"
#include "grpc++/client_context.h"
#include "grpc++/create_channel.h"
#include "grpc++/security/credentials.h"
#include "../common.h"

class GB28181Client
{
public:
	GB28181Client(std::shared_ptr<grpc::Channel> channel)
		: stub_(GB28181::Greeter::NewStub(channel)) {}

	void GetRTMPUrl(const std::string &mainId, const std::string &subId, int isTcp)
	{
		GB28181::GetRTMPUrlRequest request;
		request.set_mainid(mainId);
		request.set_subid(subId);
		request.set_istcp(isTcp);
		GB28181::GetRTMPUrlReply reply;
		grpc::ClientContext context;
		stub_->GetRTMPUrl(&context, request, &reply);
		std::cout << reply.url() << std::endl;
	}

	void GetRtspUrl(const std::string &mainId, const std::string &subId, int isTcp, std::string &rtsp_url)
	{
		GB28181::RealPlayUrlInRequest request;
		GB28181::GetDeviceStatusRequest dev_status_req;
		GB28181::RealPlayUrlInReply reply;
		GB28181::GetDeviceStatusReply dev_status_rep;
		grpc::ClientContext context;
		grpc::ClientContext status_ctx;

		request.set_mainid(mainId);
		request.set_subid(subId);
		request.set_istcp(isTcp);

		dev_status_req.set_mainid(mainId);
		dev_status_req.set_subid(subId);

		stub_->GetDeviceStatus(&status_ctx, dev_status_req, &dev_status_rep);
		stub_->GetRealPlayUrlIn(&context, request, &reply);
		std::cout << "{mainId:" << mainId << " subId:" << subId << " code: " << reply.code() << " online: " << dev_status_rep.isonline() << "}" << std::endl;
		rtsp_url = reply.url();
	}

	void getPlaybackUrl(std::string startTime, std::string endTime, std::string mainId, std::string subId)
	{
		GB28181::PlayBackUrlRequest request;
		request.set_mainid(mainId);
		request.set_subid(subId);
		request.set_starttime(startTime);
		request.set_endtime(endTime);
		GB28181::PlayBackUrlReply reply;
		grpc::ClientContext context;
		stub_->GetPlayBackUrl(&context, request, &reply);
		std::cout << reply.code() << reply.msg() << reply.rtspurl() << std::endl;
	}

	void stopVideoById(std::string mainId, std::string subId, int isTcp)
	{
		GB28181::StopVideoByIdRequest request;
		request.set_mainid(mainId);
		request.set_subid(subId);
		request.set_istcp(isTcp);
		GB28181::StopVideoByIdReply reply;
		grpc::ClientContext context;
		stub_->StopVideoById(&context, request, &reply);
		std::cout << reply.code() << reply.msg() << std::endl;
	}

	void stopVideoBySsrc(std::string ssrc)
	{
		GB28181::StopVideoBySsrcRequest request;
		request.set_ssrc(ssrc);
		GB28181::StopVideoBySsrcReply reply;
		grpc::ClientContext context;
		stub_->StopVideoBySsrc(&context, request, &reply);
		std::cout << reply.code() << reply.msg() << std::endl;
	}

	void getDeviceList()
	{
		GB28181::GetDeviceListRequest request;
		GB28181::GetDeviceListReply reply;
		grpc::ClientContext context;
		stub_->GetDeviceList(&context, request, &reply);
		std::cout << "device list : " << reply.code() << reply.msg() << std::endl;
		if (!reply.code())
		{
			int size = reply.devicelist_size();
			for (int i = 0; i < size; i++)
			{
				const GB28181::DeviceList &device = reply.devicelist(i);
				std::cout << "{ip:" << device.ip() << " mainId:" << device.mainid() << " subId:" << device.subid() << "}" << std::endl;
			}
		}
		else
		{
			std::cout << "device list : code = " << reply.code() << ", msg = " << reply.msg() << std::endl;
		}
	}

private:
	std::unique_ptr<GB28181::Greeter::Stub> stub_;
};

std::string grpcClientStart(std::string &mainId, std::string &subId)
{
	char stream_media_url[128];
	snprintf(stream_media_url, sizeof(stream_media_url), "%s:%d", streamMediaIp.c_str(), stream_media_port);

	auto channel = grpc::CreateChannel(stream_media_url, grpc::InsecureChannelCredentials());
	GB28181Client client(channel);
	std::string rtsp_url;

	// client.getDeviceList();
	client.GetRtspUrl(mainId, subId, 0, rtsp_url);
	//	client.GetRTMPUrl(mainId, subId, 1);

	return rtsp_url;
}
