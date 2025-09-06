// SPDX-FileCopyrightText: Copyright 2021 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#version 460 core
#extension GL_GOOGLE_include_directive : enable

#define CITRON_USE_FP16
#define USE_RCAS 1
#define VERSION 1

#include "fidelityfx_fsr.frag"
