#include <iostream>
#pragma once

#include <vector>
#include <QString>

// 前向声明
class ModJsonManager;

namespace fingerprint
{
  typedef std::vector<unsigned char> Buffer;
  void print_usage();
  Buffer get_jar_contents(const char *jar_file_path);
  long get_file_size(FILE *file);
  uint32_t compute_hash(Buffer &buffer);
  bool is_whitespace_character(char b);
  uint32_t compute_normalized_length(Buffer &buffer);

  // 改名为getJarFingerprint
  QString getJarFingerprint(const QString &jarPath);
  
  // 模组信息结构体
  struct ModInfo {
    QString filePath;        // 文件路径
    QString fileFingerprint; // 文件指纹
    int projectId;           // 项目ID
    int fileId;              // 文件ID
    QString name;            // 文件名
    bool isValid;            // 是否有效
    
    ModInfo() : projectId(0), fileId(0), isValid(false) {}
    ModInfo(const QString& path) : filePath(path), projectId(0), fileId(0), isValid(false) {}
  };
  
  // 处理单个模组文件信息
  ModInfo processModInfo(const ModInfo &modInfo, ModJsonManager *jsonManager = nullptr);
  
  // 批量处理模组文件信息
  QList<ModInfo> processModInfoList(const QList<ModInfo> &modInfoList, ModJsonManager *jsonManager = nullptr);
}
