#ifndef SERIALIZATION_H
#define SERIALIZATION_H
/*
 * Copyright (c) 2023 IPADS, Shanghai Jiao Tong University.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include "string.h"
#include <algorithm>
#include <cstdint>
#include <sstream>
#include <tuple>
#include <utility>
#include <vector>

typedef uint32_t length_t;

#define MARK_LEN sizeof(length_t)

class StreamBuffer : public std::vector<char> {
public:
  StreamBuffer() { m_curpos = 0; }
  StreamBuffer(const char *in, size_t len) {
    m_curpos = 0;
    insert(begin(), in, in + len);
  }
  ~StreamBuffer() {}

  void reset() { m_curpos = 0; }
  const char *data() { return &(*this)[0]; }
  const char *current() { return &(*this)[m_curpos]; }
  void offset(length_t k) { m_curpos += k; }
  bool is_eof() { return (m_curpos >= size()); }
  void input(char *in, size_t len) { insert(end(), in, in + len); }
  int findc(char c) {
    iterator itr = find(begin() + m_curpos, end(), c);
    if (itr != end()) {
      return itr - (begin() + m_curpos);
    }
    return -1;
  }

private:
  // current byte stream position
  length_t m_curpos;
};

struct PackedMigrateCallResult {
  int res;
  std::vector<char> out_buf;
};

class Serialization {
public:
  Serialization() { m_byteorder = LittleEndian; }
  ~Serialization() {}

  Serialization(StreamBuffer dev, int byteorder = LittleEndian) {
    m_byteorder = byteorder;
    m_iodevice = dev;
  }

public:
  enum ByteOrder { BigEndian, LittleEndian };

public:
  void reset() { m_iodevice.reset(); }
  int size() { return m_iodevice.size(); }
  void skip_raw_date(length_t k) { m_iodevice.offset(k); }
  const char *data() { return m_iodevice.data(); }
  void byte_orser(char *in, length_t len) {
    if (m_byteorder == BigEndian) {
      std::reverse(in, in + len);
    }
  }
  void write_raw_data(char *in, length_t len) {
    m_iodevice.input(in, len);
    m_iodevice.offset(len);
  }
  const char *current() { return m_iodevice.current(); }
  void clear() {
    m_iodevice.clear();
    reset();
  }

  template <typename T> void output_type(T &t);

  template <typename T> void input_type(T t);

  template <typename T, size_t N> void input_type(T (&t)[N]) {
    std::cout << "OK" << std::endl;
    int len = sizeof(T) * N;
    char *d = new char[len];
    const char *p = reinterpret_cast<const char *>(t);
    memcpy_s(d, len, p, len);
    byte_orser(d, len);
    m_iodevice.input(d, len);
    delete[] d;
  }

  template <typename T, size_t N> inline void output_type(T (&t)[N]) {
    std::cout << "OK" << std::endl;
    int len = sizeof(T) * N;
    if (!m_iodevice.is_eof()) {
      memcpy_s((char *)t, len, m_iodevice.current(), len);
      m_iodevice.offset(len);
      byte_orser((char *)t, len);
    }
  }

  // return x bytes of data after the current position
  void get_length_mem(char *p, length_t len) {
    if (memcpy_s(p, len, m_iodevice.current(), len) != 0) {
      return;
    }
    m_iodevice.offset(len);
  }

public:
  template <typename Tuple, std::size_t Id>
  void getv(Serialization &ds, Tuple &t) {
    ds >> std::get<Id>(t);
  }

  template <typename Tuple, std::size_t... I>
  Tuple get_tuple(std::index_sequence<I...>) {
    Tuple t;
    (void)std::initializer_list<int>{((getv<Tuple, I>(*this, t)), 0)...};
    return t;
  }

  template <typename T> Serialization &operator>>(T &i) {
    output_type(i);
    return *this;
  }

  template <typename T> Serialization &operator<<(T &&i) {
    input_type(std::forward<T>(i));
    return *this;
  }

private:
  int m_byteorder;
  StreamBuffer m_iodevice;
};

template <typename T> inline void Serialization::output_type(T &t) {
  length_t len = sizeof(T);
  char *d = new char[len];
  if (!m_iodevice.is_eof()) {
    memcpy_s(d, len, m_iodevice.current(), len);
    m_iodevice.offset(len);
    byte_orser(d, len);
    t = *reinterpret_cast<T *>(&d[0]);
  }
  delete[] d;
}

template <> inline void Serialization::output_type(std::string &in) {
  char *d = new char[MARK_LEN];
  memcpy_s(d, MARK_LEN, m_iodevice.current(), MARK_LEN);
  byte_orser(d, MARK_LEN);
  auto len = *reinterpret_cast<length_t *>(&d[0]);
  m_iodevice.offset(MARK_LEN);
  delete[] d;
  if (len == 0)
    return;
  in.insert(in.begin(), m_iodevice.current(), m_iodevice.current() + len);
  m_iodevice.offset(len);
}

template <> inline void Serialization::output_type(std::vector<char> &in) {
  char *d = new char[MARK_LEN];
  memcpy_s(d, MARK_LEN, m_iodevice.current(), MARK_LEN);
  byte_orser(d, MARK_LEN);
  auto len = *reinterpret_cast<length_t *>(&d[0]);
  m_iodevice.offset(MARK_LEN);
  delete[] d;
  if (len == 0)
    return;
  in.insert(in.begin(), m_iodevice.current(), m_iodevice.current() + len);
  m_iodevice.offset(len);
}

template <>
inline void Serialization::output_type(PackedMigrateCallResult &in) {
  output_type(in.res);
  output_type(in.out_buf);
}

template <typename T> inline void Serialization::input_type(T t) {
  length_t len = sizeof(T);
  char *d = new char[len];
  const char *p = reinterpret_cast<const char *>(&t);
  memcpy_s(d, len, p, len);
  byte_orser(d, len);
  m_iodevice.input(d, len);
  delete[] d;
}

template <> inline void Serialization::input_type(std::string in) {
  // store the string length first
  length_t len = in.size();
  char *p = reinterpret_cast<char *>(&len);
  byte_orser(p, sizeof(length_t));
  m_iodevice.input(p, sizeof(length_t));
  // store string content
  if (len <= 0)
    return;
  char *d = new char[len];
  memcpy_s(d, len, in.c_str(), len);
  m_iodevice.input(d, len);
  delete[] d;
}

template <> inline void Serialization::input_type(std::vector<char> in) {
  // store the string length first
  length_t len = in.size();
  char *p = reinterpret_cast<char *>(&len);
  byte_orser(p, sizeof(length_t));
  m_iodevice.input(p, sizeof(length_t));
  // store string content
  if (len <= 0)
    return;
  m_iodevice.input((char *)in.data(), len);
}

template <> inline void Serialization::input_type(PackedMigrateCallResult in) {
  input_type(in.res);
  input_type(in.out_buf);
}

template <> inline void Serialization::input_type(const char *in) {
  input_type<std::string>(std::string(in));
}

#endif
