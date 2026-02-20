/**
 * @file aquarium_storage.h
 * @brief 设备配置 Flash 持久化层
 *
 * 提供设备配置的保存/加载，带 magic/version/CRC32 校验。
 * 支持真实 Flash 后端（STM32）和模拟后端（单元测试）。
 */

#ifndef AQUARIUM_STORAGE_H
#define AQUARIUM_STORAGE_H

#include "aquarium_logic.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * 存储配置
 * ============================================================================
 */

#define STORAGE_MAGIC 0x41515541 /* "AQUA" in ASCII */
#define STORAGE_VERSION 1

/* ============================================================================
 * 错误码
 * ============================================================================
 */

typedef enum {
  STORAGE_OK = 0,
  STORAGE_ERR_NULL_PTR,
  STORAGE_ERR_MAGIC_MISMATCH,
  STORAGE_ERR_VERSION_MISMATCH,
  STORAGE_ERR_CRC_MISMATCH,
  STORAGE_ERR_WRITE_FAILED,
  STORAGE_ERR_ERASE_FAILED
} StorageError;

/* ============================================================================
 * 存储记录结构（带头尾校验）
 * ============================================================================
 */

typedef struct {
  uint32_t magic;   /* STORAGE_MAGIC */
  uint32_t version; /* STORAGE_VERSION */
  DeviceConfig config;
  uint32_t crc32; /* config 部分的 CRC32 */
} StorageRecord;

/* ============================================================================
 * Flash 后端接口（平台抽象）
 * ============================================================================
 */

/**
 * @brief Flash 读取回调
 * @param offset 相对存储区起始的偏移
 * @param buf    读取缓冲区
 * @param len    读取长度
 * @return 实际读取字节数
 */
typedef size_t (*StorageReadFunc)(uint32_t offset, void *buf, size_t len);

/**
 * @brief Flash 写入回调
 * @param offset 相对存储区起始的偏移
 * @param buf    写入数据
 * @param len    写入长度
 * @return 实际写入字节数
 */
typedef size_t (*StorageWriteFunc)(uint32_t offset, const void *buf,
                                   size_t len);

/**
 * @brief Flash 擦除回调
 * @return true 成功
 */
typedef bool (*StorageEraseFunc)(void);

/* ============================================================================
 * 存储上下文
 * ============================================================================
 */

typedef struct {
  StorageReadFunc read_func;
  StorageWriteFunc write_func;
  StorageEraseFunc erase_func;
} StorageContext;

/* ============================================================================
 * API
 * ============================================================================
 */

/**
 * @brief 初始化存储上下文
 */
void aqua_storage_init(StorageContext *ctx, StorageReadFunc read_fn,
                       StorageWriteFunc write_fn, StorageEraseFunc erase_fn);

/**
 * @brief 从 Flash 加载配置
 * @param ctx    存储上下文
 * @param config [输出] 配置结构
 * @return StorageError 错误码
 */
StorageError aqua_storage_load(StorageContext *ctx, DeviceConfig *config);

/**
 * @brief 保存配置到 Flash
 * @param ctx    存储上下文
 * @param config 配置结构
 * @return StorageError 错误码
 */
StorageError aqua_storage_save(StorageContext *ctx, const DeviceConfig *config);

/**
 * @brief 计算 CRC32（可供外部测试使用）
 */
uint32_t aqua_storage_crc32(const void *data, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* AQUARIUM_STORAGE_H */
