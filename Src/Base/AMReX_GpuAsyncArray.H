#ifndef AMREX_GPU_ASYNC_ARRAY_H_
#define AMREX_GPU_ASYNC_ARRAY_H_
#include <AMReX_Config.H>

#include <AMReX_Arena.H>
#include <AMReX_TypeTraits.H>
#include <AMReX_GpuDevice.H>

#include <cstddef>
#include <cstring>
#include <cstdlib>
#include <memory>

#if defined(AMREX_USE_CUDA) || defined(AMREX_USE_HIP)
extern "C" {
#if defined(AMREX_USE_HIP)
    void amrex_asyncarray_delete ( hipStream_t stream,  hipError_t error, void* p);
#elif defined(AMREX_USE_CUDA)
    void CUDART_CB amrex_asyncarray_delete (void* p);
#endif
}
#endif

namespace amrex {
namespace Gpu {

template <typename T, std::enable_if_t<std::is_trivially_copyable<T>::value,int> = 0>
class AsyncArray
{
public:

    AsyncArray (T const* h_p, const std::size_t n)
    {
        if (n == 0) return;
        h_data = static_cast<T*>(The_Pinned_Arena()->alloc(n*sizeof(T)));
        std::memcpy(h_data, h_p, n*sizeof(T));
#ifdef AMREX_USE_GPU
        if (Gpu::inLaunchRegion())
        {
            d_data = static_cast<T*>(The_Arena()->alloc(n*sizeof(T)));
            Gpu::htod_memcpy_async(d_data, h_data, n*sizeof(T));
        }
#endif
    }

    template <typename U = T, typename std::enable_if<std::is_standard_layout<U>::value && std::is_trivial<U>::value,int>::type = 0>
    explicit AsyncArray (const std::size_t n)
    {
        if (n == 0) return;
#ifdef AMREX_USE_GPU
        if (Gpu::inLaunchRegion())
        {
            d_data = static_cast<T*>(The_Arena()->alloc(n*sizeof(T)));
        }
        else
#endif
        {
            h_data = static_cast<T*>(The_Pinned_Arena()->alloc(n*sizeof(T)));
        }
    }

    ~AsyncArray () { clear(); }

    AsyncArray (AsyncArray const&) = delete;
    AsyncArray (AsyncArray &&) = delete;
    void operator= (AsyncArray const&) = delete;
    void operator= (AsyncArray &&) = delete;

    [[nodiscard]] T const* data () const noexcept { return (d_data != nullptr) ? d_data : h_data; }
    [[nodiscard]] T* data () noexcept { return (d_data != nullptr) ? d_data : h_data; }
    void clear ()
    {
#ifdef AMREX_USE_GPU
        if (Gpu::inLaunchRegion())
        {
            if (d_data != nullptr) {
#if defined(AMREX_USE_CUDA) || defined(AMREX_USE_HIP)
                T** p = static_cast<T**>(std::malloc(2*sizeof(T*)));
                p[0] = d_data;
                p[1] = h_data;
#if defined(AMREX_USE_HIP)
                AMREX_HIP_SAFE_CALL ( hipStreamAddCallback(Gpu::gpuStream(),
                                                           amrex_asyncarray_delete, p, 0));
#elif defined(AMREX_USE_CUDA)
                AMREX_CUDA_SAFE_CALL(cudaLaunchHostFunc(Gpu::gpuStream(),
                                                        amrex_asyncarray_delete, p));
#endif
#elif defined(AMREX_USE_SYCL)
#ifdef AMREX_USE_CODEPLAY_HOST_TASK
                auto* pd = d_data;
                auto* ph = h_data;
                auto& q = *(Gpu::gpuStream().queue);
                try {
                    q.submit([&] (sycl::handler& h) {
                        h.codeplay_host_task([=] () {
                            The_Arena()->free(pd);
                            The_Pinned_Arena()->free(ph);
                        });
                    });
                } catch (sycl::exception const& ex) {
                    amrex::Abort(std::string("host_task: ")+ex.what()+"!!!!!");
                }
#else
                // xxxxx SYCL todo
                Gpu::streamSynchronize();
                The_Arena()->free(d_data);
                The_Pinned_Arena()->free(h_data);
#endif
#endif
            }
        }
        else
#endif
        {
            The_Pinned_Arena()->free(h_data);
        }
        d_data = nullptr;
        h_data = nullptr;
    }

    void copyToHost (T* h_p, std::size_t n) const
    {
        if (n == 0) return;
#ifdef AMREX_USE_GPU
        if (d_data)
        {
            Gpu::dtoh_memcpy(h_p, d_data, n*sizeof(T));
        }
        else
#endif
        if (h_data)
        {
            std::memcpy(h_p, h_data, n*sizeof(T));
        }
    }

private:
    T* d_data = nullptr;
    T* h_data = nullptr;
};

}

using Gpu::AsyncArray;
}

#endif
