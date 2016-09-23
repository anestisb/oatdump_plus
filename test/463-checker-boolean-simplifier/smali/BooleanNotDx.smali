# Copyright (C) 2016 The Android Open Source Project
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

.class public LBooleanNotSmali;
.super Ljava/lang/Object;

#
# Elementary test negating a boolean. Verifies that blocks are merged and
# empty branches removed.
#

## CHECK-START: boolean BooleanNotSmali.BooleanNot(boolean) select_generator (before)
## CHECK-DAG:     <<Param:z\d+>>    ParameterValue
## CHECK-DAG:     <<Const0:i\d+>>   IntConstant 0
## CHECK-DAG:     <<Const1:i\d+>>   IntConstant 1
## CHECK-DAG:                       If [<<Param>>]
## CHECK-DAG:     <<Phi:i\d+>>      Phi [<<Const0>>,<<Const1>>]
## CHECK-DAG:                       Return [<<Phi>>]

## CHECK-START: boolean BooleanNotSmali.BooleanNot(boolean) select_generator (before)
## CHECK:                           Goto
## CHECK:                           Goto
## CHECK:                           Goto
## CHECK-NOT:                       Goto

## CHECK-START: boolean BooleanNotSmali.BooleanNot(boolean) select_generator (after)
## CHECK-DAG:     <<Param:z\d+>>    ParameterValue
## CHECK-DAG:     <<Const0:i\d+>>   IntConstant 0
## CHECK-DAG:     <<Const1:i\d+>>   IntConstant 1
## CHECK-DAG:     <<NotParam:i\d+>> Select [<<Const1>>,<<Const0>>,<<Param>>]
## CHECK-DAG:                       Return [<<NotParam>>]

## CHECK-START: boolean BooleanNotSmali.BooleanNot(boolean) select_generator (after)
## CHECK-NOT:                       If
## CHECK-NOT:                       Phi

## CHECK-START: boolean BooleanNotSmali.BooleanNot(boolean) select_generator (after)
## CHECK:                           Goto
## CHECK-NOT:                       Goto

.method public static BooleanNot(Z)Z
  .registers 2

  if-eqz v1, :true_start
  const/4 v0, 0x0

:return_start
  return v0

:true_start
  const/4 v0, 0x1
  goto :return_start

.end method
