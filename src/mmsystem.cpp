#include <windows.h>
#include <mmsystem.h>
#include <time.h>
#include <map>
#ifdef __APPLE__
#include <AudioToolbox/AudioToolbox.h>
#include <CoreFoundation/CoreFoundation.h>
#include <unistd.h>

#elif defined(__linux__)
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <fcntl.h>
#include <pthread.h>
#include <vector>
#include <mutex>
#include <atomic>
#include <memory>
#endif

#include "log.h"
#define kLogTag "mmsystem"

DWORD timeGetTime(void)
{
    DWORD uptime = 0;
    struct timespec on;
    if (clock_gettime(CLOCK_MONOTONIC, &on) == 0)
        uptime = on.tv_sec * 1000 + on.tv_nsec / 1000000;
    return uptime;
}
#define MMSYSTIME_MININTERVAL (1)
#define MMSYSTIME_MAXINTERVAL (65535)

MMRESULT timeGetDevCaps(LPTIMECAPS lpCaps, UINT wSize)
{
    if (lpCaps == 0)
    {
        SLOG_STMW() << "invalid lpCaps";
        return TIMERR_NOCANDO;
    }

    if (wSize < sizeof(TIMECAPS))
    {
        SLOG_STMW() << "invalid wSize";
        return TIMERR_NOCANDO;
    }

    lpCaps->wPeriodMin = MMSYSTIME_MININTERVAL;
    lpCaps->wPeriodMax = MMSYSTIME_MAXINTERVAL;
    return TIMERR_NOERROR;
}

MMRESULT timeBeginPeriod(UINT wPeriod)
{
    if (wPeriod < MMSYSTIME_MININTERVAL || wPeriod > MMSYSTIME_MAXINTERVAL)
        return TIMERR_NOCANDO;

    if (wPeriod > MMSYSTIME_MININTERVAL)
    {
        SLOG_STMW() << "Stub; we set our timer resolution at minimum";
    }

    return 0;
}

MMRESULT timeEndPeriod(UINT wPeriod)
{
    if (wPeriod < MMSYSTIME_MININTERVAL || wPeriod > MMSYSTIME_MAXINTERVAL)
        return TIMERR_NOCANDO;

    if (wPeriod > MMSYSTIME_MININTERVAL)
    {
        SLOG_STMW() << "Stub; we set our timer resolution at minimum";
    }
    return 0;
}

#ifdef __APPLE__
class AudioPlayer {
  private:
    SystemSoundID m_soundID;
    AudioPlayer()
        : m_soundID(0)
    {
    }

  public:
    BOOL play(LPCSTR pszSound, BOOL bPurge)
    {
        BOOL ret = FALSE;
        do
        {
            CFStringRef soundPath = CFStringCreateWithCString(NULL, pszSound, kCFStringEncodingUTF8);
            if (soundPath == NULL)
            {
                break;
            }

            CFURLRef soundURL = CFURLCreateWithFileSystemPath(NULL, soundPath, kCFURLPOSIXPathStyle, false);
            if (soundURL == NULL)
            {
                CFRelease(soundPath);
                break;
            }
            if (bPurge && m_soundID)
            {
                AudioServicesDisposeSystemSoundID(m_soundID);
            }
            m_soundID = 0;
            OSStatus status = AudioServicesCreateSystemSoundID(soundURL, &m_soundID);
            if (status == noErr)
            {
                AudioServicesPlaySystemSound(m_soundID);
                ret = TRUE;
            }
            CFRelease(soundURL);
            CFRelease(soundPath);
        } while (false);
        return ret;
    }
    static AudioPlayer *getInstance()
    {
        static AudioPlayer instance;
        return &instance;
    }
};
#elif defined(__linux__)
#ifdef HAS_ALSA // HAS_ALSA
#include <alsa/asoundlib.h>
// WAV文件头部结构 - RIFF chunk
struct RiffHeader
{
    char riff[4];      // "RIFF"
    uint32_t fileSize; // 文件大小-8
    char wave[4];      // "WAVE"
};

// fmt chunk
struct FmtChunk
{
    char fmt[4];            // "fmt "
    uint32_t fmtSize;       // fmt块大小(通常16或18)
    uint16_t format;        // 格式类别(1=PCM)
    uint16_t channels;      // 声道数
    uint32_t samplesPerSec; // 采样频率
    uint32_t bytesPerSec;   // 波形数据传输速率
    uint16_t blockAlign;    // 数据块对齐单位
    uint16_t bitsPerSample; // 采样精度
};

// data chunk header
struct DataChunkHeader
{
    char data[4];      // "data"
    uint32_t dataSize; // 数据块大小
};

struct PlayStatus
{
    std::atomic<bool> bStop;
    std::atomic<bool> bDone;
};

// 播放线程参数
struct PlayThreadArg
{
    char *fileName;
    std::shared_ptr<PlayStatus> status;
};

class AudioPlayer {
  private:
    std::mutex m_mutex;
    // use shared_ptr to manage stop flags and avoid ownership races
    std::map<pthread_t, std::shared_ptr<PlayStatus>> m_threads;

    AudioPlayer()
    {
    }

    ~AudioPlayer()
    {
        stopAll();
    }

    // 跳过WAV文件中的非data chunk
    static bool seekToDataChunk(int fd, DataChunkHeader &dataHeader)
    {
        char chunkId[4];
        uint32_t chunkSize;

        while (read(fd, chunkId, 4) == 4)
        {
            if (read(fd, &chunkSize, 4) != 4)
            {
                return false;
            }

            if (memcmp(chunkId, "data", 4) == 0)
            {
                memcpy(dataHeader.data, chunkId, 4);
                dataHeader.dataSize = chunkSize;
                return true;
            }

            // 跳过这个chunk
            if (lseek(fd, chunkSize, SEEK_CUR) == -1)
            {
                return false;
            }
        }
        return false;
    }

    // 音频播放线程函数
    static void *playThread(void *arg)
    {
        PlayThreadArg *threadArg = (PlayThreadArg *)arg;
        char *fileName = threadArg->fileName;
        std::shared_ptr<PlayStatus> status = threadArg->status;
        delete threadArg;

        snd_pcm_t *pcmHandle = nullptr;
        int fd = -1;

        // 打开WAV文件
        fd = open(fileName, O_RDONLY);
        if (fd < 0)
        {
            SLOG_STMW() << "Failed to open audio file: " << fileName;
            goto cleanup;
        }

        {
            // 读取RIFF头
            RiffHeader riffHeader;
            if (read(fd, &riffHeader, sizeof(RiffHeader)) != sizeof(RiffHeader))
            {
                SLOG_STMW() << "Failed to read RIFF header from file: " << fileName;
                goto cleanup;
            }

            // 检查是否为有效的WAV文件
            if (memcmp(riffHeader.riff, "RIFF", 4) != 0 || memcmp(riffHeader.wave, "WAVE", 4) != 0)
            {
                SLOG_STMW() << "Invalid WAV file: " << fileName;
                goto cleanup;
            }

            // 读取fmt chunk
            FmtChunk fmtChunk;
            if (read(fd, &fmtChunk, sizeof(FmtChunk)) != sizeof(FmtChunk))
            {
                SLOG_STMW() << "Failed to read fmt chunk from file: " << fileName;
                goto cleanup;
            }

            if (memcmp(fmtChunk.fmt, "fmt ", 4) != 0)
            {
                SLOG_STMW() << "Invalid fmt chunk in file: " << fileName;
                goto cleanup;
            }

            // 跳过fmt chunk中的扩展数据
            if (fmtChunk.fmtSize > 16)
            {
                lseek(fd, fmtChunk.fmtSize - 16, SEEK_CUR);
            }

            // 查找data chunk
            DataChunkHeader dataHeader;
            if (!seekToDataChunk(fd, dataHeader))
            {
                SLOG_STMW() << "Failed to find data chunk in file: " << fileName;
                goto cleanup;
            }

            // 打开ALSA PCM设备
            int err = snd_pcm_open(&pcmHandle, "default", SND_PCM_STREAM_PLAYBACK, 0);
            if (err < 0)
            {
                SLOG_STMW() << "Failed to open ALSA device: " << snd_strerror(err);
                goto cleanup;
            }

            // 设置硬件参数
            snd_pcm_hw_params_t *hwParams;
            snd_pcm_hw_params_alloca(&hwParams);
            snd_pcm_hw_params_any(pcmHandle, hwParams);

            snd_pcm_hw_params_set_access(pcmHandle, hwParams, SND_PCM_ACCESS_RW_INTERLEAVED);

            // 设置格式
            snd_pcm_format_t format;
            switch (fmtChunk.bitsPerSample)
            {
            case 8:
                format = SND_PCM_FORMAT_U8;
                break;
            case 16:
                format = SND_PCM_FORMAT_S16_LE;
                break;
            case 24:
                format = SND_PCM_FORMAT_S24_LE;
                break;
            case 32:
                format = SND_PCM_FORMAT_S32_LE;
                break;
            default:
                SLOG_STMW() << "Unsupported bits per sample: " << fmtChunk.bitsPerSample;
                goto cleanup;
            }
            snd_pcm_hw_params_set_format(pcmHandle, hwParams, format);
            snd_pcm_hw_params_set_channels(pcmHandle, hwParams, fmtChunk.channels);

            unsigned int rate = fmtChunk.samplesPerSec;
            snd_pcm_hw_params_set_rate_near(pcmHandle, hwParams, &rate, nullptr);

            err = snd_pcm_hw_params(pcmHandle, hwParams);
            if (err < 0)
            {
                SLOG_STMW() << "Failed to set ALSA hw params: " << snd_strerror(err);
                goto cleanup;
            }

            snd_pcm_prepare(pcmHandle);

            // 读取并播放音频数据
            const int bufferSize = 4096;
            char buffer[bufferSize];
            ssize_t bytesRead;
            snd_pcm_uframes_t frames;

            while (!status->bStop.load() && (bytesRead = read(fd, buffer, bufferSize)) > 0)
            {
                frames = bytesRead / fmtChunk.blockAlign;
                snd_pcm_sframes_t written = snd_pcm_writei(pcmHandle, buffer, frames);
                if (written < 0)
                {
                    written = snd_pcm_recover(pcmHandle, written, 0);
                }
                if (written < 0)
                {
                    SLOG_STMW() << "ALSA write error: " << snd_strerror(written);
                    break;
                }
            }

            // 等待播放完成
            if (!status->bStop.load())
            {
                snd_pcm_drain(pcmHandle);
            }
        }

    cleanup:
        if (pcmHandle)
        {
            snd_pcm_close(pcmHandle);
        }
        if (fd >= 0)
        {
            close(fd);
        }
        free(fileName);
        status->bDone.store(true);
        return nullptr;
    }

    void stopAll()
    {
        // Move thread list to local so we don't hold the mutex while joining
        std::map<pthread_t, std::shared_ptr<PlayStatus>> local;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            for (auto &pair : m_threads)
            {
                pair.second->bStop.store(true);
            }
            // transfer ownership
            local.swap(m_threads);
        }

        // join threads without holding the mutex (prevents deadlock with threads trying to erase their entry)
        for (auto &pair : local)
        {
            pthread_join(pair.first, nullptr);
        }

        // shared_ptrs in local will be destroyed here
        local.clear();
    }

  public:
    BOOL play(LPCSTR pszSound, BOOL bPurge)
    {
        // 如果是停止播放请求
        if (pszSound == NULL || pszSound[0] == '\0')
        {
            stopAll();
            return TRUE;
        }

        // 如果需要清除当前播放
        if (bPurge)
        {
            stopAll();
        }

        // 清理已完成的线程
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            for (auto it = m_threads.begin(); it != m_threads.end();)
            {
                if (it->second->bDone.load())
                {
                    pthread_join(it->first, nullptr);
                    it = m_threads.erase(it);
                }
                else
                {
                    ++it;
                }
            }
        }

        // 创建停止标志 (shared_ptr 管理生命周期)
        auto status = std::make_shared<PlayStatus>();
        status->bDone.store(false);
        status->bStop.store(false);

        // 创建播放线程参数
        PlayThreadArg *threadArg = new PlayThreadArg();
        threadArg->fileName = strdup(pszSound);
        threadArg->status = status;

        // 创建播放线程
        pthread_t thread;
        if (pthread_create(&thread, NULL, playThread, threadArg) == 0)
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_threads[thread] = status;
            return TRUE;
        }
        else
        {
            SLOG_STMW() << "Failed to create audio playback thread";
            free(threadArg->fileName);
            delete threadArg;
            return FALSE;
        }
    }

    static AudioPlayer *getInstance()
    {
        static AudioPlayer instance;
        return &instance;
    }
};
#else  // HAS_ALSA
class AudioPlayer {
  private:
    pid_t m_soundID;
    AudioPlayer()
        : m_soundID(0)
    {
    }

  public:
    BOOL play(LPCSTR pszSound, BOOL bPurge)
    {
        if (bPurge || pszSound == NULL || pszSound[0] == '\0')
        {
            if (m_soundID != 0)
            {
                kill(m_soundID, SIGTERM);
                waitpid(m_soundID, NULL, WNOHANG);
                m_soundID = 0;
            }
            if (pszSound == NULL || pszSound[0] == '\0')
                return TRUE;
        }

        pid_t pid = fork();
        if (pid < 0)
        {
            return FALSE;
        }
        if (pid == 0)
        {
            execlp("aplay", "aplay", pszSound, (char *)NULL);
            _exit(127);
        }
        m_soundID = pid;
        return TRUE;
    }
    static AudioPlayer *getInstance()
    {
        static AudioPlayer instance;
        return &instance;
    }
};
#endif // HAS_ALSA

#endif
BOOL PlaySound(LPCSTR pszSound, HMODULE hmod, DWORD fdwSound)
{
    if (fdwSound & (SND_MEMORY | SND_RESOURCE | SND_ALIAS | SND_ALIAS_ID))
    {
        return FALSE;
    }
#if defined(__APPLE__) || defined(__linux__)
    return AudioPlayer::getInstance()->play(pszSound, fdwSound & SND_PURGE);
#else
    // For other platforms, return FALSE as before
    return FALSE;
#endif
}
