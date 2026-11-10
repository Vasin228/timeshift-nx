// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>
#include <fmt/format.h>
#include <rex/filesystem.h>
#include <rex/logging.h>
#include <rex/ui/vulkan/util.h>
namespace thps::native {
// One bounded diagnostic frame. Finish/Release require all recorded work to
// have completed; callers wait their existing submission fences first.
class ResolveCapture {
 public:
  static uint32_t BytesPerPixel(VkFormat f) {
    switch(f) {
      case VK_FORMAT_R8_UNORM:return 1;
      case VK_FORMAT_R8G8_UNORM:case VK_FORMAT_R16_UNORM:case VK_FORMAT_R16_SFLOAT:return 2;
      case VK_FORMAT_R8G8B8A8_UNORM:case VK_FORMAT_A2B10G10R10_UNORM_PACK32:
      case VK_FORMAT_R16G16_UNORM:case VK_FORMAT_R16G16_SNORM:case VK_FORMAT_R16G16_SFLOAT:
      case VK_FORMAT_R32_SFLOAT:return 4;
      case VK_FORMAT_R16G16B16A16_SNORM:case VK_FORMAT_R16G16B16A16_UNORM:
      case VK_FORMAT_R16G16B16A16_SFLOAT:case VK_FORMAT_R32G32_SFLOAT:return 8;
      case VK_FORMAT_R32G32B32A32_SFLOAT:return 16;
      default:return 0;
    }
  }
  bool Begin(const rex::ui::vulkan::VulkanDevice* device,
             std::filesystem::path directory,uint64_t frame,
             VkDeviceSize budget=96ull<<20) {
    if(device_||!budget)return false;
    std::error_code error;std::filesystem::create_directories(directory,error);
    if(error)return false;
    if(!rex::ui::vulkan::util::CreateDedicatedAllocationBuffer(device,budget,
        VK_BUFFER_USAGE_TRANSFER_DST_BIT,rex::ui::vulkan::util::MemoryPurpose::kReadback,
        buffer_,memory_))return false;
    device_=device;directory_=std::move(directory);frame_=frame;budget_=budget;
    return true;
  }
  bool active() const {return device_!=nullptr;}
  bool Record(VkCommandBuffer commands,VkImage image,VkFormat format,
              uint32_t width,uint32_t height,uint32_t address,bool depth,
              uint64_t draw,std::string label) {
    uint32_t bpp=BytesPerPixel(format);
    uint64_t bytes=uint64_t(width)*height*bpp;
    VkDeviceSize offset=(used_+255)&~VkDeviceSize(255);
    if(!device_||!image||!bpp||!width||!height||width>8192||height>8192||entries_.size()>=48||
       bytes>budget_||offset>budget_-bytes){++skipped_;return false;}
    const auto& fn=device_->functions();
    VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER,nullptr,
        VK_ACCESS_MEMORY_WRITE_BIT,VK_ACCESS_TRANSFER_READ_BIT};
    fn.vkCmdPipelineBarrier(commands,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT,0,1,&barrier,0,nullptr,0,nullptr);
    VkBufferImageCopy copy{};copy.bufferOffset=offset;
    copy.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};copy.imageExtent={width,height,1};
    fn.vkCmdCopyImageToBuffer(commands,image,VK_IMAGE_LAYOUT_GENERAL,buffer_,1,&copy);
    barrier.srcAccessMask=VK_ACCESS_TRANSFER_READ_BIT;
    barrier.dstAccessMask=VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT;
    fn.vkCmdPipelineBarrier(commands,VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,0,1,&barrier,0,nullptr,0,nullptr);
    entries_.push_back({offset,bytes,width,height,address,uint32_t(format),depth,draw,std::move(label)});
    used_=offset+bytes;return true;
  }
  bool Finish() {
    if(!device_)return false;
    const auto& fn=device_->functions();auto vk=device_->device();void* mapped=nullptr;
    bool ok=fn.vkMapMemory(vk,memory_,0,VK_WHOLE_SIZE,0,&mapped)==VK_SUCCESS;
    if(ok){
      VkMappedMemoryRange range{VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE};range.memory=memory_;range.size=VK_WHOLE_SIZE;
      ok=fn.vkInvalidateMappedMemoryRanges(vk,1,&range)==VK_SUCCESS;
      if(ok){
        FILE* manifest=rex::filesystem::OpenFile(directory_/"manifest.tsv","wb");
        ok=manifest!=nullptr;
        if(manifest){
          std::fprintf(manifest,"# frame=%llu skipped=%u bytes=%llu\nindex\tfile\taddress\twidth\theight\tformat\tdepth\tdraw\tlabel\n",
              (unsigned long long)frame_,skipped_,(unsigned long long)used_);
          for(size_t i=0;i<entries_.size();++i){const auto& e=entries_[i];
            auto filename=fmt::format("{:02d}-{:08X}.raw",i,e.address);
            FILE* out=rex::filesystem::OpenFile(directory_/filename,"wb");
            bool written=false;
            if(out){written=std::fwrite(static_cast<const uint8_t*>(mapped)+e.offset,size_t(e.bytes),1,out)==1;
              if(std::fclose(out)!=0)written=false;}
            ok&=written;
            std::fprintf(manifest,"%zu\t%s\t%08X\t%u\t%u\t%u\t%u\t%llu\t%s\n",i,filename.c_str(),e.address,e.width,e.height,e.format,unsigned(e.depth),(unsigned long long)e.draw,e.label.c_str());
          }
          if(std::fclose(manifest)!=0)ok=false;
        }
      }
      fn.vkUnmapMemory(vk,memory_);
    }
    REXLOG_INFO("[native] N177 capture frame={} images={} skipped={} MiB={:.1f} success={} path={}",
        frame_,entries_.size(),skipped_,double(used_)/1048576.0,ok,directory_.string());
    Release();return ok;
  }
  void Release(){
    if(!device_)return;
    const auto& fn=device_->functions();auto vk=device_->device();
    fn.vkDestroyBuffer(vk,buffer_,nullptr);fn.vkFreeMemory(vk,memory_,nullptr);
    buffer_=VK_NULL_HANDLE;memory_=VK_NULL_HANDLE;device_=nullptr;entries_.clear();used_=0;
  }
 private:
  struct Entry{VkDeviceSize offset;uint64_t bytes;uint32_t width,height,address,format;bool depth;uint64_t draw;std::string label;};
  const rex::ui::vulkan::VulkanDevice* device_=nullptr;
  VkBuffer buffer_=VK_NULL_HANDLE;VkDeviceMemory memory_=VK_NULL_HANDLE;
  std::filesystem::path directory_;uint64_t frame_=0;VkDeviceSize budget_=0,used_=0;
  std::vector<Entry> entries_;uint32_t skipped_=0;
};
} // namespace thps::native
