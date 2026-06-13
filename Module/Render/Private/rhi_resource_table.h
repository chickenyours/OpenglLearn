#pragma once

#include <vector>
#include <queue>
#include <optional>
#include <utility>

#include "Render/Public/rhi_resource_handle.h"

namespace Render{

    template <typename T>
    class ResourceTable
    {
    private:
        std::vector<std::optional<T>> datas;
        std::vector<uint32_t> versions;
        std::queue<uint32_t> recycleID;

    private:
        uint32_t NextID()
        {
            if (!recycleID.empty())
            {
                uint32_t nextId = recycleID.front();
                recycleID.pop();
                return nextId;
            }

            const uint32_t nextId = static_cast<uint32_t>(datas.size());

            datas.emplace_back(std::nullopt);
            versions.emplace_back(0);

            return nextId;
        }

        bool IsAlive(const RenderResourceHandle<T>& handle) const
        {
            return handle.id < datas.size()
                && handle.id < versions.size()
                && versions[handle.id] == handle.version
                && datas[handle.id].has_value();
        }

    public:
        T* Get(const RenderResourceHandle<T>& handle)
        {
            if (!IsAlive(handle))
            {
                return nullptr;
            }

            return &datas[handle.id].value();
        }

        const T* Get(const RenderResourceHandle<T>& handle) const
        {
            if (!IsAlive(handle))
            {
                return nullptr;
            }

            return &datas[handle.id].value();
        }

        RenderResourceHandle<T> Add(const T& val)
        {
            const uint32_t nextId = NextID();

            datas[nextId].emplace(val);

            return RenderResourceHandle<T>{
                nextId,
                versions[nextId]
            };
        }

        RenderResourceHandle<T> Add(T&& val)
        {
            const uint32_t nextId = NextID();

            datas[nextId].emplace(std::move(val));

            return RenderResourceHandle<T>{
                nextId,
                versions[nextId]
            };
        }

        template <typename... Args>
        RenderResourceHandle<T> Emplace(Args&&... args)
        {
            const uint32_t nextId = NextID();

            datas[nextId].emplace(std::forward<Args>(args)...);

            return RenderResourceHandle<T>{
                nextId,
                versions[nextId]
            };
        }

        void Remove(const RenderResourceHandle<T>& handle)
        {
            if (!IsAlive(handle))
            {
                return;
            }

            datas[handle.id].reset();
            ++versions[handle.id];
            recycleID.push(handle.id);
        }

        bool Contains(const RenderResourceHandle<T>& handle) const
        {
            return IsAlive(handle);
        }

        void Clear()
        {
            datas.clear();
            versions.clear();

            std::queue<uint32_t> empty;
            recycleID.swap(empty);
        }

        size_t Capacity() const
        {
            return datas.size();
        }
    };

}