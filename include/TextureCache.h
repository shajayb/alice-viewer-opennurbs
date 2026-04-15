#ifndef TEXTURE_CACHE_H
#define TEXTURE_CACHE_H

#include <glad/glad.h>
#include <string>
#include <unordered_map>
#include <vector>
#include <mutex>

namespace Alice
{
    struct CachedTexture
    {
        GLuint handle;
        int refCount;
    };

    class TextureCache
    {
    public:
        static TextureCache& Get()
        {
            static TextureCache instance;
            return instance;
        }

        GLuint Acquire(const std::string& key, const uint8_t* data, int width, int height, int channels)
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (m_cache.count(key))
            {
                m_cache[key].refCount++;
                return m_cache[key].handle;
            }

            GLuint handle;
            glGenTextures(1, &handle);
            glBindTexture(GL_TEXTURE_2D, handle);
            
            GLenum format = GL_RGB;
            if (channels == 1) format = GL_RED;
            else if (channels == 3) format = GL_RGB;
            else if (channels == 4) format = GL_RGBA;

            glTexImage2D(GL_TEXTURE_2D, 0, format, width, height, 0, format, GL_UNSIGNED_BYTE, data);
            glGenerateMipmap(GL_TEXTURE_2D);

            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

            m_cache[key] = { handle, 1 };
            return handle;
        }

        void Release(const std::string& key)
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (m_cache.count(key))
            {
                m_cache[key].refCount--;
                if (m_cache[key].refCount <= 0)
                {
                    glDeleteTextures(1, &m_cache[key].handle);
                    m_cache.erase(key);
                }
            }
        }

        void Release(GLuint handle)
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            for (auto it = m_cache.begin(); it != m_cache.end(); ++it)
            {
                if (it->second.handle == handle)
                {
                    it->second.refCount--;
                    if (it->second.refCount <= 0)
                    {
                        glDeleteTextures(1, &it->second.handle);
                        m_cache.erase(it);
                    }
                    return;
                }
            }
        }

    private:
        TextureCache() {}
        std::unordered_map<std::string, CachedTexture> m_cache;
        std::mutex m_mutex;
    };
}

#endif
