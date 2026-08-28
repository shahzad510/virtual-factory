#include "eip_session.hh"

#include <libplctag.h>

#include <sstream>
#include <utility>

namespace virtual_factory
{
namespace internal
{

EipSession::~EipSession()
{
  this->close();
}

bool EipSession::open(const EipSessionConfig &config)
{
  this->close();

  if (config.host.empty() || config.port == 0 || config.plcType.empty())
  {
    this->captureError("missing EtherNet/IP host, port, or plc type");
    return false;
  }

  this->config_ = config;
  this->open_ = true;
  this->last_error_.clear();
  return true;
}

void EipSession::close()
{
  this->destroyAllTags();
  this->open_ = false;
}

bool EipSession::connected() const
{
  return this->open_;
}

bool EipSession::createTag(
    const std::string &key,
    const std::string &tagName,
    EipTagValueType valueType)
{
  if (!this->ensureOpen())
  {
    return false;
  }
  if (key.empty() || tagName.empty())
  {
    this->captureError("empty tag key or name");
    return false;
  }
  if (this->tags_.count(key) != 0)
  {
    this->captureError("duplicate tag key");
    return false;
  }

  const std::string attrib = this->buildAttrib(tagName);
  const int32_t tagId = plc_tag_create(attrib.c_str(), this->timeoutMs());
  if (tagId < 0)
  {
    this->captureError(tagId, "tag create failed");
    return false;
  }

  const int status = plc_tag_status(tagId);
  if (status != PLCTAG_STATUS_OK)
  {
    this->captureError(status, "tag create status");
    (void)plc_tag_destroy(tagId);
    return false;
  }

  TagEntry entry;
  entry.id = tagId;
  entry.valueType = valueType;
  this->tags_.emplace(key, entry);
  this->last_error_.clear();
  return true;
}

void EipSession::destroyAllTags()
{
  for (auto &entry : this->tags_)
  {
    if (entry.second.id >= 0)
    {
      (void)plc_tag_destroy(entry.second.id);
      entry.second.id = -1;
    }
  }
  this->tags_.clear();
}

bool EipSession::readBool(const std::string &key, bool *value)
{
  if (value == nullptr)
  {
    return false;
  }
  TagEntry *entry = this->findTag(key);
  if (entry == nullptr)
  {
    return false;
  }

  const int rc = plc_tag_read(entry->id, this->timeoutMs());
  if (rc != PLCTAG_STATUS_OK)
  {
    this->captureError(rc, "tag read failed");
    return false;
  }

  if (entry->valueType == EipTagValueType::Bool)
  {
    *value = plc_tag_get_uint8(entry->id, 0) != 0;
  }
  else if (entry->valueType == EipTagValueType::Dint)
  {
    *value = plc_tag_get_int32(entry->id, 0) != 0;
  }
  else
  {
    *value = plc_tag_get_float32(entry->id, 0) != 0.0f;
  }
  this->last_error_.clear();
  return true;
}

bool EipSession::readDint(const std::string &key, std::int32_t *value)
{
  if (value == nullptr)
  {
    return false;
  }
  TagEntry *entry = this->findTag(key);
  if (entry == nullptr)
  {
    return false;
  }

  const int rc = plc_tag_read(entry->id, this->timeoutMs());
  if (rc != PLCTAG_STATUS_OK)
  {
    this->captureError(rc, "tag read failed");
    return false;
  }

  if (entry->valueType == EipTagValueType::Bool)
  {
    *value = plc_tag_get_uint8(entry->id, 0) != 0 ? 1 : 0;
  }
  else if (entry->valueType == EipTagValueType::Real)
  {
    *value = static_cast<std::int32_t>(plc_tag_get_float32(entry->id, 0));
  }
  else
  {
    *value = plc_tag_get_int32(entry->id, 0);
  }
  this->last_error_.clear();
  return true;
}

bool EipSession::readReal(const std::string &key, float *value)
{
  if (value == nullptr)
  {
    return false;
  }
  TagEntry *entry = this->findTag(key);
  if (entry == nullptr)
  {
    return false;
  }

  const int rc = plc_tag_read(entry->id, this->timeoutMs());
  if (rc != PLCTAG_STATUS_OK)
  {
    this->captureError(rc, "tag read failed");
    return false;
  }

  if (entry->valueType == EipTagValueType::Bool)
  {
    *value = plc_tag_get_uint8(entry->id, 0) != 0 ? 1.0f : 0.0f;
  }
  else if (entry->valueType == EipTagValueType::Dint)
  {
    *value = static_cast<float>(plc_tag_get_int32(entry->id, 0));
  }
  else
  {
    *value = plc_tag_get_float32(entry->id, 0);
  }
  this->last_error_.clear();
  return true;
}

bool EipSession::writeBool(const std::string &key, bool value)
{
  TagEntry *entry = this->findTag(key);
  if (entry == nullptr)
  {
    return false;
  }

  int setRc = PLCTAG_STATUS_OK;
  if (entry->valueType == EipTagValueType::Bool)
  {
    setRc = plc_tag_set_uint8(entry->id, 0, value ? 1 : 0);
  }
  else if (entry->valueType == EipTagValueType::Dint)
  {
    setRc = plc_tag_set_int32(entry->id, 0, value ? 1 : 0);
  }
  else
  {
    setRc = plc_tag_set_float32(entry->id, 0, value ? 1.0f : 0.0f);
  }
  if (setRc != PLCTAG_STATUS_OK)
  {
    this->captureError(setRc, "tag set failed");
    return false;
  }

  const int rc = plc_tag_write(entry->id, this->timeoutMs());
  if (rc != PLCTAG_STATUS_OK)
  {
    this->captureError(rc, "tag write failed");
    return false;
  }
  this->last_error_.clear();
  return true;
}

bool EipSession::writeDint(const std::string &key, std::int32_t value)
{
  TagEntry *entry = this->findTag(key);
  if (entry == nullptr)
  {
    return false;
  }

  int setRc = PLCTAG_STATUS_OK;
  if (entry->valueType == EipTagValueType::Bool)
  {
    setRc = plc_tag_set_uint8(entry->id, 0, value != 0 ? 1 : 0);
  }
  else if (entry->valueType == EipTagValueType::Real)
  {
    setRc = plc_tag_set_float32(entry->id, 0, static_cast<float>(value));
  }
  else
  {
    setRc = plc_tag_set_int32(entry->id, 0, value);
  }
  if (setRc != PLCTAG_STATUS_OK)
  {
    this->captureError(setRc, "tag set failed");
    return false;
  }

  const int rc = plc_tag_write(entry->id, this->timeoutMs());
  if (rc != PLCTAG_STATUS_OK)
  {
    this->captureError(rc, "tag write failed");
    return false;
  }
  this->last_error_.clear();
  return true;
}

bool EipSession::writeReal(const std::string &key, float value)
{
  TagEntry *entry = this->findTag(key);
  if (entry == nullptr)
  {
    return false;
  }

  int setRc = PLCTAG_STATUS_OK;
  if (entry->valueType == EipTagValueType::Bool)
  {
    setRc = plc_tag_set_uint8(entry->id, 0, value != 0.0f ? 1 : 0);
  }
  else if (entry->valueType == EipTagValueType::Dint)
  {
    setRc = plc_tag_set_int32(entry->id, 0, static_cast<std::int32_t>(value));
  }
  else
  {
    setRc = plc_tag_set_float32(entry->id, 0, value);
  }
  if (setRc != PLCTAG_STATUS_OK)
  {
    this->captureError(setRc, "tag set failed");
    return false;
  }

  const int rc = plc_tag_write(entry->id, this->timeoutMs());
  if (rc != PLCTAG_STATUS_OK)
  {
    this->captureError(rc, "tag write failed");
    return false;
  }
  this->last_error_.clear();
  return true;
}

void EipSession::setTimeoutMs(int timeoutMs)
{
  this->config_.timeoutMs = timeoutMs < 1 ? 1 : timeoutMs;
}

std::string EipSession::lastError() const
{
  return this->last_error_;
}

bool EipSession::ensureOpen()
{
  if (!this->open_)
  {
    this->captureError("EtherNet/IP session is closed");
    return false;
  }
  return true;
}

EipSession::TagEntry *EipSession::findTag(const std::string &key)
{
  auto it = this->tags_.find(key);
  if (it == this->tags_.end() || it->second.id < 0)
  {
    this->captureError("unknown or destroyed tag");
    return nullptr;
  }
  return &it->second;
}

std::string EipSession::buildAttrib(const std::string &tagName) const
{
  std::ostringstream out;
  out << "protocol=ab_eip"
      << "&gateway=" << this->config_.host << ':' << this->config_.port
      << "&plc=" << this->config_.plcType
      << "&elem_count=1"
      << "&name=" << tagName;
  if (!this->config_.path.empty())
  {
    out << "&path=" << this->config_.path;
  }
  return out.str();
}

void EipSession::captureError(int rc, const char *prefix)
{
  this->last_error_ = std::string(prefix) + ": " + plc_tag_decode_error(rc);
}

void EipSession::captureError(const char *message)
{
  this->last_error_ = message;
}

int EipSession::timeoutMs() const
{
  return this->config_.timeoutMs < 1 ? 1 : this->config_.timeoutMs;
}

}  // namespace internal
}  // namespace virtual_factory
