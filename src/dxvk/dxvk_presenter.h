#pragma once

#include <functional>
#include <optional>
#include <queue>
#include <vector>

#include "../util/log/log.h"

#include "../util/util_error.h"
#include "../util/util_fps_limiter.h"
#include "../util/util_math.h"
#include "../util/util_string.h"

#include "../util/sync/sync_signal.h"

#include "../vulkan/vulkan_loader.h"

#include "dxvk_format.h"
#include "dxvk_image.h"

namespace dxvk {

  using PresenterSurfaceProc = std::function<VkResult (VkSurfaceKHR*)>;

  class DxvkDevice;

  /**
   * \brief Presenter description
   * 
   * Contains the desired properties of
   * the swap chain. This is passed as
   * an input during swap chain creation.
   */
  struct PresenterDesc {
    bool deferSurfaceCreation = false;
  };

  /**
   * \brief Presenter semaphores
   * 
   * Pair of semaphores used for acquire and present
   * operations, including the command buffers used
   * in between. Also stores a fence to signal on
   * image acquisition.
   */
  struct PresenterSync {
    VkSemaphore acquire = VK_NULL_HANDLE;
    VkSemaphore present = VK_NULL_HANDLE;
    VkFence fence = VK_NULL_HANDLE;
    VkBool32 fenceSignaled = VK_FALSE;
  };

  /**
   * \brief Queued frame
   */
  struct PresenterFrame {
    uint64_t          frameId = 0u;
    VkPresentModeKHR  mode    = VK_PRESENT_MODE_FIFO_KHR;
    VkResult          result  = VK_NOT_READY;
  };

  /**
   * \brief Format compatibility list
   */
  struct PresenterFormatList {
    VkColorSpaceKHR colorSpace;
    size_t formatCount;
    const VkFormat* formats;
  };

  /**
   * \brief Vulkan presenter
   * 
   * Provides abstractions for some of the
   * more complicated aspects of Vulkan's
   * window system integration.
   */
  class Presenter : public RcObject {

  public:

    Presenter(
      const Rc<DxvkDevice>&   device,
      const Rc<sync::Signal>& signal,
      const PresenterDesc&    desc,
            PresenterSurfaceProc&& proc);
    
    ~Presenter();

    /**
     * \brief Tests swap chain status
     *
     * If no swapchain currently exists, this method may create
     * one so that presentation can subsequently be performed.
     * \returns One of the following return codes:
     *  - \c VK_SUCCESS if a valid swapchain exists
     *  - \c VK_NOT_READY if no swap chain can be created
     *  - Any other error code if swap chain creation failed.
     */
    VkResult checkSwapChainStatus();

    /**
     * \brief Acquires next image
     *
     * Tries to acquire an image from the underlying Vulkan
     * swapchain. May recreate the swapchain if any surface
     * properties or user-specified parameters have changed.
     * Potentially blocks the calling thread, and must not be
     * called if any present call is currently in flight.
     * \param [out] sync Synchronization semaphores
     * \param [out] image Acquired swap chain image
     * \returns Status of the operation. May return
     *    \c VK_NOT_READY if no swap chain exists.
     */
    VkResult acquireNextImage(
            PresenterSync&  sync,
            Rc<DxvkImage>&  image);
    
    /**
     * \brief Presents current image
     * 
     * Presents the last successfuly acquired image.
     * \param [in] frameId Frame number.
     *    Must increase monotonically.
     * \returns Status of the operation
     */
    VkResult presentImage(uint64_t frameId);

    /**
     * \brief Signals a given frame
     *
     * Waits for the present operation to complete and then signals
     * the presenter signal with the given frame ID. Must not be
     * called before GPU work prior to the present submission has
     * completed in order to maintain consistency.
     * \param [in] result Presentation result
     * \param [in] frameId Frame number
     */
    void signalFrame(VkResult result, uint64_t frameId);

    /**
     * \brief Changes sync interval
     *
     * Changes the Vulkan present mode as necessary.
     * \param [in] syncInterval New sync interval
     */
    void setSyncInterval(uint32_t syncInterval);

    /**
     * \brief Changes maximum frame rate
     *
     * \param [in] frameRate Target frame rate. Set
     *    to 0 in order to disable the limiter.
     */
    void setFrameRateLimit(double frameRate, uint32_t maxLatency);

    /**
     * \brief Sets preferred color space and format
     *
     * If the Vulkan surface does not natively support the given
     * parameter combo, it will try to select a format and color
     * space with similar properties.
     * \param [in] format Preferred surface format
     */
    void setSurfaceFormat(VkSurfaceFormatKHR format);

    /**
     * \brief Sets preferred surface extent
     *
     * The preferred surface extent is only relevant if the Vulkan
     * surface itself does not have a fixed size. Should match the
     * back buffer size of the application.
     * \param [in] extent Preferred surface extent
     */
    void setSurfaceExtent(VkExtent2D extent);

    /**
     * \brief Sets HDR metadata
     *
     * Updated HDR metadata will be applied on the next \c acquire.
     * \param [in] hdrMetadata HDR Metadata
     */
    void setHdrMetadata(VkHdrMetadataEXT hdrMetadata);

    /**
     * \brief Checks support for a Vulkan color space
     *
     * \param [in] colorspace The color space to test
     * \returns \c true if the Vulkan surface supports the colorspace
     */
    bool supportsColorSpace(VkColorSpaceKHR colorspace);

    /**
     * \brief Invalidates Vulkan surface
     *
     * This will cause the Vulkan surface to be destroyed and
     * recreated on the next \c acquire call. This is a hacky
     * workaround to support windows with multiple surfaces.
     */
    void invalidateSurface();

  private:

    Rc<DxvkDevice>              m_device;
    Rc<sync::Signal>            m_signal;

    Rc<vk::InstanceFn>          m_vki;
    Rc<vk::DeviceFn>            m_vkd;

    dxvk::mutex                 m_surfaceMutex;
    PresenterSurfaceProc        m_surfaceProc;

    VkSurfaceKHR                m_surface     = VK_NULL_HANDLE;
    VkSwapchainKHR              m_swapchain   = VK_NULL_HANDLE;

    VkFullScreenExclusiveEXT    m_fullscreenMode = VK_FULL_SCREEN_EXCLUSIVE_DISALLOWED_EXT;

    std::vector<Rc<DxvkImage>>  m_images;
    std::vector<PresenterSync>  m_semaphores;

    std::vector<VkPresentModeKHR> m_dynamicModes;

    VkExtent2D                  m_preferredExtent = { };
    VkSurfaceFormatKHR          m_preferredFormat = { };
    uint32_t                    m_preferredSyncInterval = 1u;

    bool                        m_dirtySwapchain = false;
    bool                        m_dirtySurface = false;

    VkPresentModeKHR            m_presentMode = VK_PRESENT_MODE_FIFO_KHR;

    uint32_t                    m_imageIndex = 0;
    uint32_t                    m_frameIndex = 0;

    VkResult                    m_acquireStatus = VK_NOT_READY;

    std::optional<VkHdrMetadataEXT> m_hdrMetadata;
    bool                        m_hdrMetadataDirty = false;

    alignas(CACHE_LINE_SIZE)
    dxvk::mutex                 m_frameMutex;
    dxvk::condition_variable    m_frameCond;
    dxvk::thread                m_frameThread;
    std::queue<PresenterFrame>  m_frameQueue;

    std::atomic<uint64_t>       m_lastFrameId = { 0ull };

    alignas(CACHE_LINE_SIZE)
    FpsLimiter                  m_fpsLimiter;

    void updateSwapChain();

    VkResult recreateSwapChain();

    VkResult createSwapChain();

    VkResult getSupportedFormats(
            std::vector<VkSurfaceFormatKHR>& formats) const;
    
    VkResult getSupportedPresentModes(
            std::vector<VkPresentModeKHR>& modes) const;
    
    VkResult getSwapImages(
            std::vector<VkImage>&     images);
    
    VkSurfaceFormatKHR pickSurfaceFormat(
            uint32_t                  numSupported,
      const VkSurfaceFormatKHR*       pSupported,
      const VkSurfaceFormatKHR&       desired);

    VkColorSpaceKHR pickColorSpace(
            uint32_t                  numSupported,
      const VkSurfaceFormatKHR*       pSupported,
            VkColorSpaceKHR           desired);

    VkFormat pickFormat(
            uint32_t                  numSupported,
      const VkSurfaceFormatKHR*       pSupported,
            VkColorSpaceKHR           colorSpace,
            VkFormat                  format);

    VkPresentModeKHR pickPresentMode(
            uint32_t                  numSupported,
      const VkPresentModeKHR*         pSupported,
            uint32_t                  syncInterval);

    VkExtent2D pickImageExtent(
      const VkSurfaceCapabilitiesKHR& caps,
            VkExtent2D                desired);

    uint32_t pickImageCount(
            uint32_t                  minImageCount,
            uint32_t                  maxImageCount);

    VkResult createSurface();

    void destroySwapchain();

    void destroySurface();

    void waitForSwapchainFence(
            PresenterSync&            sync);

    void runFrameThread();

  };

}
