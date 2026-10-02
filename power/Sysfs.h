/*
 * Copyright (C) 2016 The Android Open Source Project
 * Copyright (C) 2018 The LineageOS Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#ifndef cityman_POWER_SYSFS_H
#define cityman_POWER_SYSFS_H

#include <string>

namespace cityman {
namespace power {

int writeNode(const char *path, const char *value);
int writeIntNode(const char *path, int value);
int readNode(const char *path, std::string *out);
int readFile(const char *path, std::string *out);
int readIntNode(const char *path, int *out);
bool nodeExists(const char *path);

}  // namespace power
}  // namespace cityman

#endif
