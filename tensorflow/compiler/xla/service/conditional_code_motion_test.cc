/* Copyright 2017 The TensorFlow Authors. All Rights Reserved.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
==============================================================================*/

#include "tensorflow/compiler/xla/service/conditional_code_motion.h"

#include <string>
#include <utility>

#include "tensorflow/compiler/xla/literal_util.h"
#include "tensorflow/compiler/xla/service/hlo_computation.h"
#include "tensorflow/compiler/xla/service/hlo_instruction.h"
#include "tensorflow/compiler/xla/service/hlo_matchers.h"
#include "tensorflow/compiler/xla/service/hlo_opcode.h"
#include "tensorflow/compiler/xla/shape_util.h"
#include "tensorflow/compiler/xla/test.h"
#include "tensorflow/compiler/xla/tests/hlo_test_base.h"
#include "tensorflow/compiler/xla/types.h"
#include "tensorflow/compiler/xla/xla_data.pb.h"
#include "tensorflow/core/lib/core/status.h"
#include "tensorflow/core/lib/core/status_test_util.h"
#include "tensorflow/core/platform/types.h"

namespace xla {
namespace conditional_opt {

using ConditionalCodeMotionTest = HloTestBase;
namespace op = xla::testing::opcode_matchers;

TEST_F(ConditionalCodeMotionTest, MoveSubsetTupleOut) {
  absl::string_view hlo_string =
      R"(
HloModule RemoveDotOpOut

on_true {
  %arg_tuple.1 = (f32[93184,4]{1,0}) parameter(0)
  %get-tuple-element.1 = f32[93184,4]{1,0} get-tuple-element(%arg_tuple.1), index=0
  %reshape.8493 = f32[2,512,364]{2,1,0} reshape(f32[93184,4]{1,0} %get-tuple-element.1)
  %convert.2894 = bf16[2,512,364]{2,1,0} convert(f32[2,512,364]{2,1,0} %reshape.8493)
  ROOT %tuple.1 = ( bf16[2,512,364]{2,1,0}, f32[2,512,364]{2,1,0}) tuple(%convert.2894, %reshape.8493)
}

on_false {
  %arg_tuple.2 = (f32[93184,4]{1,0}) parameter(0)
  %get-tuple-element.3 = f32[93184,4]{1,0} get-tuple-element(%arg_tuple.2), index=0
  %reshape.9717 = f32[2,512,364]{2,1,0} reshape(f32[93184,4]{1,0} %get-tuple-element.3)
  %add = f32[2,512,364]{2,1,0} add(f32[2,512,364]{2,1,0} %reshape.9717, f32[2,512,364]{2,1,0} %reshape.9717)
  %convert.3604 = bf16[2,512,364]{2,1,0} convert(f32[2,512,364]{2,1,0} %reshape.9717), metadata={op_type="Cast" op_name="gradients/Cast_125_grad/Cast"}
  ROOT %tuple.2 = (bf16[2,512,364]{2,1,0}, f32[2,512,364]{2,1,0}) tuple(%convert.3604, %add)
}

ENTRY main {
  pred.1 = pred[] parameter(0)
  arg_tuple.11 = (f32[93184,4]{1,0}) parameter(1)
  arg_tuple.22 = (f32[93184,4]{1,0}) parameter(2)
  conditional = (bf16[2,512,364]{2,1,0}, f32[2,512,364]{2,1,0}) conditional(pred.1, arg_tuple.11, arg_tuple.22), true_computation=on_true, false_computation=on_false
  get-first-index = bf16[2,512,364]{2,1,0} get-tuple-element(conditional), index=0
  get-first-index.2 = f32[2,512,364]{2,1,0} get-tuple-element(conditional), index=1
  ROOT result = (bf16[2,512,364]{2,1,0}, f32[2,512,364]{2,1,0}) tuple(get-first-index, get-first-index.2)
}
)";
  auto module = ParseAndReturnVerifiedModule(hlo_string).ValueOrDie();
  ConditionalCodeMotion pass(true, true);
  ASSERT_TRUE(pass.Run(&*module).ValueOrDie());

  HloInstruction* root = module->entry_computation()->root_instruction();
  EXPECT_THAT(root, AllOf(op::Tuple(op::Convert(), op::GetTupleElement())));
}

TEST_F(ConditionalCodeMotionTest, MoveConvertOutConditionalRoot) {
  absl::string_view hlo_string =
      R"(
HloModule RemoveDotOpOut

on_true {
  %arg_tuple.1 = (f32[93184,4]{1,0}) parameter(0)
  %get-tuple-element.1 = f32[93184,4]{1,0} get-tuple-element(%arg_tuple.1), index=0
  %reshape.8493 = f32[2,512,364]{2,1,0} reshape(f32[93184,4]{1,0} %get-tuple-element.1)
  %add.8493 = f32[2,512,364]{2,1,0} add(f32[2,512,364]{2,1,0} %reshape.8493, f32[2,512,364]{2,1,0} %reshape.8493)
  %convert.2894 = bf16[2,512,364]{2,1,0} convert(f32[2,512,364]{2,1,0} %add.8493)
  ROOT %tuple.1 = ( bf16[2,512,364]{2,1,0}) tuple(%convert.2894)
}

on_false {
  %arg_tuple.2 = (f32[93184,4]{1,0}) parameter(0)
  %get-tuple-element.3 = f32[93184,4]{1,0} get-tuple-element(%arg_tuple.2), index=0
  %reshape.9717 = f32[2,512,364]{2,1,0} reshape(f32[93184,4]{1,0} %get-tuple-element.3)
  %add.8493 = f32[2,512,364]{2,1,0} add(f32[2,512,364]{2,1,0} %reshape.9717, f32[2,512,364]{2,1,0} %reshape.9717)
  %sub.8493 = f32[2,512,364]{2,1,0} subtract(f32[2,512,364]{2,1,0} %add.8493, f32[2,512,364]{2,1,0} %reshape.9717)
  %convert.3604 = bf16[2,512,364]{2,1,0} convert(f32[2,512,364]{2,1,0} %reshape.9717), metadata={op_type="Cast" op_name="gradients/Cast_125_grad/Cast"}
  ROOT %tuple.2 = (bf16[2,512,364]{2,1,0}) tuple(%convert.3604)
}

ENTRY main {
  pred.1 = pred[] parameter(0)
  arg_tuple.11 = (f32[93184,4]{1,0}) parameter(1)
  arg_tuple.22 = (f32[93184,4]{1,0}) parameter(2)
  ROOT conditional = (bf16[2,512,364]{2,1,0}) conditional(pred.1, arg_tuple.11, arg_tuple.22), true_computation=on_true, false_computation=on_false
}
)";
  auto module = ParseAndReturnVerifiedModule(hlo_string).ValueOrDie();
  ConditionalCodeMotion pass(true, true);
  ASSERT_TRUE(pass.Run(&*module).ValueOrDie());

  HloInstruction* root = module->entry_computation()->root_instruction();
  EXPECT_THAT(root, AllOf(op::Tuple(op::Convert())));
}

TEST_F(ConditionalCodeMotionTest, MoveConvertOutConditional) {
  absl::string_view hlo_string =
      R"(
HloModule RemoveDotOpOut

on_true {
  %arg_tuple.1 = (f32[93184,4]{1,0}) parameter(0)
  %get-tuple-element.1 = f32[93184,4]{1,0} get-tuple-element(%arg_tuple.1), index=0
  %reshape.8493 = f32[2,512,364]{2,1,0} reshape(f32[93184,4]{1,0} %get-tuple-element.1)
  %add.8493 = f32[2,512,364]{2,1,0} add(f32[2,512,364]{2,1,0} %reshape.8493, f32[2,512,364]{2,1,0} %reshape.8493)
  %convert.2894 = bf16[2,512,364]{2,1,0} convert(f32[2,512,364]{2,1,0} %add.8493)
  ROOT %tuple.1 = ( bf16[2,512,364]{2,1,0}) tuple(%convert.2894)
}

on_false {
  %arg_tuple.2 = (f32[93184,4]{1,0}) parameter(0)
  %get-tuple-element.3 = f32[93184,4]{1,0} get-tuple-element(%arg_tuple.2), index=0
  %reshape.9717 = f32[2,512,364]{2,1,0} reshape(f32[93184,4]{1,0} %get-tuple-element.3)
  %add.8493 = f32[2,512,364]{2,1,0} add(f32[2,512,364]{2,1,0} %reshape.9717, f32[2,512,364]{2,1,0} %reshape.9717)
  %sub.8493 = f32[2,512,364]{2,1,0} subtract(f32[2,512,364]{2,1,0} %add.8493, f32[2,512,364]{2,1,0} %reshape.9717)
  %convert.3604 = bf16[2,512,364]{2,1,0} convert(f32[2,512,364]{2,1,0} %reshape.9717), metadata={op_type="Cast" op_name="gradients/Cast_125_grad/Cast"}
  ROOT %tuple.2 = (bf16[2,512,364]{2,1,0}) tuple(%convert.3604)
}

ENTRY main {
  pred.1 = pred[] parameter(0)
  arg_tuple.11 = (f32[93184,4]{1,0}) parameter(1)
  arg_tuple.22 = (f32[93184,4]{1,0}) parameter(2)
  conditional = (bf16[2,512,364]{2,1,0}) conditional(pred.1, arg_tuple.11, arg_tuple.22), true_computation=on_true, false_computation=on_false
  get-first-index = bf16[2,512,364]{2,1,0} get-tuple-element(conditional), index=0
  ROOT result = (bf16[2,512,364]{2,1,0}) tuple(get-first-index)
}
)";
  auto module = ParseAndReturnVerifiedModule(hlo_string).ValueOrDie();
  ConditionalCodeMotion pass(true, true);
  ASSERT_TRUE(pass.Run(&*module).ValueOrDie());

  HloInstruction* root = module->entry_computation()->root_instruction();
  EXPECT_THAT(root, AllOf(op::Tuple(op::Convert())));
}

TEST_F(ConditionalCodeMotionTest, ConditionalShapeNotMutable) {
  absl::string_view hlo_string =
      R"(
HloModule RemoveDotOpOut

on_true {
  %arg_tuple.1 = (f32[93184,4]{1,0}) parameter(0)
  %get-tuple-element.1 = f32[93184,4]{1,0} get-tuple-element(%arg_tuple.1), index=0
  %reshape.8493 = f32[2,512,364]{2,1,0} reshape(f32[93184,4]{1,0} %get-tuple-element.1)
  %add.8493 = f32[2,512,364]{2,1,0} add(f32[2,512,364]{2,1,0} %reshape.8493, f32[2,512,364]{2,1,0} %reshape.8493)
  %convert.2894 = bf16[2,512,364]{2,1,0} convert(f32[2,512,364]{2,1,0} %add.8493)
  ROOT %tuple.1 = ( bf16[2,512,364]{2,1,0}) tuple(%convert.2894)
}

on_false {
  %arg_tuple.2 = (f32[93184,4]{1,0}) parameter(0)
  %get-tuple-element.3 = f32[93184,4]{1,0} get-tuple-element(%arg_tuple.2), index=0
  %reshape.9717 = f32[2,512,364]{2,1,0} reshape(f32[93184,4]{1,0} %get-tuple-element.3)
  %add.8493 = f32[2,512,364]{2,1,0} add(f32[2,512,364]{2,1,0} %reshape.9717, f32[2,512,364]{2,1,0} %reshape.9717)
  %sub.8493 = f32[2,512,364]{2,1,0} subtract(f32[2,512,364]{2,1,0} %add.8493, f32[2,512,364]{2,1,0} %reshape.9717)
  %convert.3604 = bf16[2,512,364]{2,1,0} convert(f32[2,512,364]{2,1,0} %reshape.9717), metadata={op_type="Cast" op_name="gradients/Cast_125_grad/Cast"}
  ROOT %tuple.2 = (bf16[2,512,364]{2,1,0}) tuple(%convert.3604)
}

ENTRY main {
  pred.1 = pred[] parameter(0)
  arg_tuple.11 = (f32[93184,4]{1,0}) parameter(1)
  arg_tuple.22 = (f32[93184,4]{1,0}) parameter(2)
  conditional = (bf16[2,512,364]{2,1,0}) conditional(pred.1, arg_tuple.11, arg_tuple.22), true_computation=on_true, false_computation=on_false
  get-first-index = bf16[2,512,364]{2,1,0} get-tuple-element(conditional), index=0
  ROOT result = (bf16[2,512,364]{2,1,0}, (bf16[2,512,364]{2,1,0})) tuple(get-first-index, conditional)
}
)";
  auto module = ParseAndReturnVerifiedModule(hlo_string).ValueOrDie();
  ConditionalCodeMotion pass(true, true);
  ASSERT_FALSE(pass.Run(&*module).ValueOrDie());
}

TEST_F(ConditionalCodeMotionTest, MoveConvertOut) {
  absl::string_view hlo_string =
      R"(
HloModule RemoveDotOpOut

on_true {
  %arg_tuple.1 = (f32[93184,4]{1,0}) parameter(0)
  %get-tuple-element.1 = f32[93184,4]{1,0} get-tuple-element(%arg_tuple.1), index=0
  %reshape.8493 = f32[2,512,364]{2,1,0} reshape(f32[93184,4]{1,0} %get-tuple-element.1)
  %convert.2894 = bf16[2,512,364]{2,1,0} convert(f32[2,512,364]{2,1,0} %reshape.8493)
  ROOT %tuple.1 = ( bf16[2,512,364]{2,1,0}) tuple(%convert.2894)
}

on_false {
  %arg_tuple.2 = (f32[93184,4]{1,0}) parameter(0)
  %get-tuple-element.3 = f32[93184,4]{1,0} get-tuple-element(%arg_tuple.2), index=0
  %reshape.9717 = f32[2,512,364]{2,1,0} reshape(f32[93184,4]{1,0} %get-tuple-element.3)
  %convert.3604 = bf16[2,512,364]{2,1,0} convert(f32[2,512,364]{2,1,0} %reshape.9717), metadata={op_type="Cast" op_name="gradients/Cast_125_grad/Cast"}
  ROOT %tuple.2 = (bf16[2,512,364]{2,1,0}) tuple(%convert.3604)
}

ENTRY main {
  pred.1 = pred[] parameter(0)
  arg_tuple.11 = (f32[93184,4]{1,0}) parameter(1)
  arg_tuple.22 = (f32[93184,4]{1,0}) parameter(2)
  conditional = (bf16[2,512,364]{2,1,0}) conditional(pred.1, arg_tuple.11, arg_tuple.22), true_computation=on_true, false_computation=on_false
  get-first-index = bf16[2,512,364]{2,1,0} get-tuple-element(conditional), index=0
  add.1 = bf16[2,512,364]{2,1,0} add(bf16[2,512,364]{2,1,0} get-first-index, bf16[2,512,364]{2,1,0} get-first-index)
  ROOT result = (bf16[2,512,364]{2,1,0}) tuple(add.1)
}
)";
  auto module = ParseAndReturnVerifiedModule(hlo_string).ValueOrDie();
  ConditionalCodeMotion pass(true, true);
  ASSERT_TRUE(pass.Run(&*module).ValueOrDie());

  const HloInstruction* conditional =
      FindInstruction(module.get(), "conditional");
  const HloComputation* on_true = conditional->branch_computation(0);
  ASSERT_EQ(on_true->instruction_count(), 1);
  const HloComputation* on_false = conditional->branch_computation(1);
  ASSERT_EQ(on_false->instruction_count(), 1);

  HloInstruction* root = module->entry_computation()->root_instruction();
  EXPECT_THAT(
      root,
      AllOf(op::Tuple(op::Add(
          op::Convert(op::Reshape(op::GetTupleElement(op::Conditional()))),
          op::Convert(op::Reshape(op::GetTupleElement(op::Conditional())))))));
}

TEST_F(ConditionalCodeMotionTest, UserShareOperandCannotBeMoved) {
  absl::string_view hlo_string =
      R"(
HloModule RemoveIdenticalInstruction

on_true {
  arg_tuple.1 = (f32[]) parameter(0)
  get-tuple-element.1 = f32[] get-tuple-element(arg_tuple.1), index=0
  constant.1 = f32[] constant(1)
  constant.2 = f32[] constant(2)
  constant.3 = f32[] constant(3)
  constant.4 = f32[] constant(4)
  constant.5 = f32[] constant(5)
  add.1 = f32[] add(get-tuple-element.1, constant.1)
  add.2 = f32[] add(add.1, constant.2)
  add.3 = f32[] add(add.1, constant.3)
  add.4 = f32[] add(add.3, constant.5)
  multiply.1 = f32[] multiply(add.4, constant.4)
  ROOT tuple.6 = (f32[], f32[]) tuple(multiply.1, add.4)
}

on_false {
  arg_tuple.2 = (f32[]) parameter(0)
  get-tuple-element.2 = f32[] get-tuple-element(arg_tuple.2), index=0
  constant.6 = f32[] constant(1)
  constant.7 = f32[] constant(2)
  constant.8 = f32[] constant(3)
  constant.9 = f32[] constant(4)
  constant.10 = f32[] constant(5)
  add.4 = f32[] add(get-tuple-element.2, constant.6)
  sub.1 = f32[] subtract(add.4, constant.7)
  add.5 = f32[] add(add.4, constant.8)
  add.6 = f32[] add(add.5, constant.10)
  multiply.2 = f32[] multiply(sub.1, constant.9)
  ROOT tuple.6 = (f32[], f32[]) tuple(multiply.2, add.6)
}

ENTRY main {
  pred.1 = pred[] parameter(0)
  tuple.1 = (f32[]) parameter(1)
  tuple.2 = (f32[]) parameter(2)
  conditional = (f32[], f32[])
    conditional(pred.1, tuple.1, tuple.2), true_computation=on_true,
    false_computation=on_false
  get-first-index = f32[] get-tuple-element(conditional), index=0
  get-second-index = f32[] get-tuple-element(conditional), index=1
  ROOT result = f32[] add(get-first-index, get-second-index)
}
)";
  auto module = ParseAndReturnVerifiedModule(hlo_string).ValueOrDie();
  ConditionalCodeMotion pass(true, true);
  ASSERT_TRUE(pass.Run(&*module).ValueOrDie());

  const HloInstruction* conditional =
      FindInstruction(module.get(), "conditional");
  const HloComputation* on_true = conditional->branch_computation(0);
  ASSERT_EQ(on_true->instruction_count(), 9);
  const HloComputation* on_false = conditional->branch_computation(1);
  ASSERT_EQ(on_false->instruction_count(), 9);

  HloInstruction* root = module->entry_computation()->root_instruction();
  EXPECT_THAT(root,
              AllOf(op::Add(op::Multiply(op::GetTupleElement(op::Conditional()),
                                         op::Constant()),
                            op::GetTupleElement(op::Conditional()))));
}

TEST_F(ConditionalCodeMotionTest, ConditionalRootElementChanged) {
  absl::string_view hlo_string =
      R"(
HloModule RemoveIdenticalInstruction

on_true {
  arg_tuple.1 = (f32[]) parameter(0)
  get-tuple-element.1 = f32[] get-tuple-element(arg_tuple.1), index=0
  constant.1 = f32[] constant(1)
  constant.2 = f32[] constant(2)
  add.1 = f32[] add(get-tuple-element.1, constant.1)
  add.2 = f32[] add(get-tuple-element.1, constant.2)
  add.3 = f32[] add(add.1, add.2)
  ROOT tuple.3 = (f32[]) tuple(add.3)
}

on_false {
  arg_tuple.2 = (f32[]) parameter(0)
  get-tuple-element.2 = f32[] get-tuple-element(arg_tuple.2), index=0
  constant.3 = f32[] constant(1)
  constant.4 = f32[] constant(2)
  add.4 = f32[] add(constant.4, constant.3)
  add.5 = f32[] add(get-tuple-element.2, constant.4)
  add.6 = f32[] add(add.4, add.5)
  ROOT tuple.4 = (f32[]) tuple(add.6)
}

ENTRY main {
  pred.1 = pred[] parameter(0)
  tuple.1 = (f32[]) parameter(1)
  tuple.2 = (f32[]) parameter(2)
  conditional = (f32[])
    conditional(pred.1, tuple.1, tuple.2), true_computation=on_true,
    false_computation=on_false
  get-first-index = f32[] get-tuple-element(conditional), index=0
  ROOT result = f32[] add(get-first-index, get-first-index)
}
)";
  auto module = ParseAndReturnVerifiedModule(hlo_string).ValueOrDie();
  ConditionalCodeMotion pass(true, true);
  ASSERT_TRUE(pass.Run(&*module).ValueOrDie());
  const HloInstruction* conditional =
      FindInstruction(module.get(), "conditional");
  const HloComputation* on_true = conditional->branch_computation(0);
  ASSERT_EQ(on_true->instruction_count(), 1);
  const HloComputation* on_false = conditional->branch_computation(1);
  ASSERT_EQ(on_false->instruction_count(), 3);

  HloInstruction* root = module->entry_computation()->root_instruction();
  EXPECT_THAT(
      root,
      AllOf(op::Add(
          op::Add(
              op::Add(op::GetTupleElement(op::Conditional()), op::Constant()),
              op::Add(op::GetTupleElement(op::Conditional()), op::Constant())),
          op::Add(
              op::Add(op::GetTupleElement(op::Conditional()), op::Constant()),
              op::Add(op::GetTupleElement(op::Conditional()),
                      op::Constant())))));
}

TEST_F(ConditionalCodeMotionTest, ConditionalIsRootInstruction) {
  absl::string_view hlo_string =
      R"(
HloModule RemoveIdenticalInstruction

on_true {
  arg_tuple.1 = (f32[]) parameter(0)
  get-tuple-element.1 = f32[] get-tuple-element(arg_tuple.1), index=0
  constant.1 = f32[] constant(1)
  constant.2 = f32[] constant(2)
  constant.3 = f32[] constant(3)
  constant.4 = f32[] constant(4)
  constant.5 = f32[] constant(5)
  add.1 = f32[] add(get-tuple-element.1, constant.1)
  add.2 = f32[] add(add.1, constant.2)
  add.3 = f32[] add(add.1, constant.3)
  add.4 = f32[] add(add.3, constant.5)
  multiply.1 = f32[] multiply(add.2, constant.4)
  ROOT tuple.6 = (f32[], f32[]) tuple(multiply.1, add.4)
}

on_false {
  arg_tuple.2 = (f32[]) parameter(0)
  get-tuple-element.2 = f32[] get-tuple-element(arg_tuple.2), index=0
  constant.6 = f32[] constant(1)
  constant.7 = f32[] constant(2)
  constant.8 = f32[] constant(3)
  constant.9 = f32[] constant(4)
  constant.10 = f32[] constant(5)
  add.4 = f32[] add(get-tuple-element.2, constant.6)
  sub.1 = f32[] subtract(add.4, constant.7)
  add.5 = f32[] add(add.4, constant.8)
  add.6 = f32[] add(add.5, constant.10)
  multiply.2 = f32[] multiply(sub.1, constant.9)
  ROOT tuple.6 = (f32[], f32[]) tuple(multiply.2, add.6)
}

ENTRY main {
  pred.1 = pred[] parameter(0)
  tuple.1 = (f32[]) parameter(1)
  tuple.2 = (f32[]) parameter(2)
  ROOT conditional = (f32[], f32[])
    conditional(pred.1, tuple.1, tuple.2), true_computation=on_true,
    false_computation=on_false
}
)";
  auto module = ParseAndReturnVerifiedModule(hlo_string).ValueOrDie();
  ConditionalCodeMotion pass(true, true);
  // If there is no instruction after the conditional, there is no benefit to
  // move
  ASSERT_FALSE(pass.Run(&*module).ValueOrDie());
}

TEST_F(ConditionalCodeMotionTest, LayoutMisMatchCannotMovedOut) {
  absl::string_view hlo_string =
      R"(
HloModule LayoutMisMatchCannotMovedOut

%add.64 (x.139: bf16[], y.139: bf16[]) -> bf16[] {
  %x.139 = bf16[]{:T(512)} parameter(0)
  %y.139 = bf16[]{:T(512)} parameter(1)
  ROOT %add.44073 = bf16[]{:T(512)} add(bf16[]{:T(512)} %x.139, bf16[]{:T(512)} %y.139)
}

%add.181 (x.256: bf16[], y.256: bf16[]) -> bf16[] {
  %x.256 = bf16[]{:T(512)} parameter(0)
  %y.256 = bf16[]{:T(512)} parameter(1)
  ROOT %add.44842 = bf16[]{:T(512)} add(bf16[]{:T(512)} %x.256, bf16[]{:T(512)} %y.256)
}

on_true {
  %arg_tuple.1 = (bf16[93184,4]{1,0}) parameter(0)
  %get-tuple-element.1 = bf16[93184,4]{1,0} get-tuple-element(%arg_tuple.1), index=0
  %all-reduce.1 = bf16[93184,4]{1,0}
    all-reduce(bf16[93184,4]{1,0} %get-tuple-element.1),
    channel_id=188, replica_groups={{0,1}}, use_global_device_ids=true,
    to_apply=%add.64
  %convert.2894 = f32[93184,4]{1,0} convert(bf16[93184, 4]{1,0} %all-reduce.1)
  ROOT %tuple.1 = (f32[93184,4]{1,0}) tuple(%convert.2894)
}

on_false {
  %arg_tuple.2 = (bf16[93184,4]{1,0}) parameter(0)
  %get-tuple-element.3 = bf16[93184,4]{1,0} get-tuple-element(%arg_tuple.2), index=0
  %copy.1 = bf16[93184,4]{0,1} copy(bf16[93184,4]{1,0} %get-tuple-element.3)
  %all-reduce.2 = bf16[93184,4]{0, 1}
    all-reduce(bf16[93184,4]{0, 1} %copy.1),
    channel_id=188, replica_groups={{0,1}}, use_global_device_ids=true,
    to_apply=%add.181
  %convert.3604 = f32[93184,4]{0,1} convert(bf16[93184,4]{0,1} %all-reduce.2)
  ROOT %tuple.2 = (f32[93184,4]{0,1}) tuple(%convert.3604)
}

ENTRY main {
  pred.1 = pred[] parameter(0)
  arg_tuple.11 = (bf16[93184,4]{1,0}) parameter(1)
  arg_tuple.22 = (bf16[93184,4]{1,0}) parameter(2)
  conditional = (f32[93184,4]{1,0}) conditional(pred.1, arg_tuple.11, arg_tuple.22), true_computation=on_true, false_computation=on_false
  get-first-index = f32[93184,4]{1,0} get-tuple-element(conditional), index=0
  ROOT result = (f32[93184,4]{1,0}) tuple(get-first-index)
}
)";

  auto module = ParseAndReturnVerifiedModule(hlo_string).ValueOrDie();
  ConditionalCodeMotion pass(true, true);
  ASSERT_FALSE(pass.Run(&*module).ValueOrDie());
}

TEST_F(ConditionalCodeMotionTest, MoveCrossModuleAllReduceOut) {
  absl::string_view hlo_string =
      R"(
HloModule RemoveIdenticalInstruction

%add.64 (x.139: bf16[], y.139: bf16[]) -> bf16[] {
  %x.139 = bf16[]{:T(512)} parameter(0)
  %y.139 = bf16[]{:T(512)} parameter(1)
  ROOT %add.44073 = bf16[]{:T(512)} add(bf16[]{:T(512)} %x.139, bf16[]{:T(512)} %y.139)
}

%add.181 (x.256: bf16[], y.256: bf16[]) -> bf16[] {
  %x.256 = bf16[]{:T(512)} parameter(0)
  %y.256 = bf16[]{:T(512)} parameter(1)
  ROOT %add.44842 = bf16[]{:T(512)} add(bf16[]{:T(512)} %x.256, bf16[]{:T(512)} %y.256)
}

on_true {
  arg_tuple.1 = (bf16[2,54,168,128], bf16[2,52,168,128]) parameter(0)
  get-tuple-element.11 = bf16[2,54,168,128] get-tuple-element(arg_tuple.1), index=0
  get-tuple-element.12 = bf16[2,52,168,128] get-tuple-element(arg_tuple.1), index=1
  convolution.1 = bf16[3,3,128,128] convolution(bf16[2,54,168,128]
    get-tuple-element.11, bf16[2,52,168,128]
    get-tuple-element.12), window={size=52x168 pad=0_0x1_1},
    dim_labels=f01b_i01o->01bf
  all-reduce.1 = bf16[3,3,128,128]
    all-reduce(bf16[3,3,128,128] %convolution.1),
    channel_id=188, replica_groups={{0,1}}, use_global_device_ids=true,
    to_apply=%add.64, metadata={op_type="Conv2DBackpropFilter"
    op_name="gradients/resnet50/conv2d_22/Conv2D_grad/Conv2DBackpropFilter"}
  convert.1 = f32[3,3,128,128] convert(bf16[3,3,128,128] %all-reduce.1),
    metadata={op_type="Cast" op_name="Cast_15"}
  ROOT tuple.1 = (f32[3,3,128,128]) tuple(convert.1)
}

on_false {
  arg_tuple.2 = (bf16[2,86,104,128], bf16[2,84,104,128]) parameter(0)
  get-tuple-element.21 = bf16[2,86,104,128]
    get-tuple-element(arg_tuple.2), index=0
  get-tuple-element.22 = bf16[2,84,104,128]
    get-tuple-element(arg_tuple.2), index=1
  convolution.2 = bf16[3,3,128,128]
    convolution(bf16[2,86,104,128] get-tuple-element.21, bf16[2,84,104,128]
    get-tuple-element.22), window={size=84x104 pad=0_0x1_1},
    dim_labels=f01b_i01o->01bf
  all-reduce.2 = bf16[3,3,128,128]
    all-reduce(bf16[3,3,128,128] %convolution.2),
    channel_id=485, replica_groups={{0,1}}, use_global_device_ids=true,
    to_apply=%add.181, metadata={op_type="Conv2DBackpropFilter"
    op_name="gradients/resnet50/conv2d_22/Conv2D_grad/Conv2DBackpropFilter"}
  convert.2 = f32[3,3,128,128]
    convert(bf16[3,3,128,128] %all-reduce.2),
    metadata={op_type="Cast" op_name="Cast_15"}
  ROOT tuple.2 = (f32[3,3,128,128]) tuple(convert.2)
}

ENTRY main {
  pred.1 = pred[] parameter(0)
  arg_tuple.3 = (bf16[2,54,168,128], bf16[2,52,168,128]) parameter(1)
  arg_tuple.4 = (bf16[2,86,104,128], bf16[2,84,104,128]) parameter(2)
  arg_tuple.5 = f32[3,3,128,128] parameter(3)
  conditional = (f32[3,3,128,128])
    conditional(pred.1, arg_tuple.3, arg_tuple.4), true_computation=on_true,
    false_computation=on_false
  get-first-index = f32[3,3,128,128]
    get-tuple-element(conditional), index=0
  add.1 = f32[3,3,128,128] add(f32[3,3,128,128] get-first-index, f32[3,3,128,128] get-first-index)
  ROOT result = (f32[3,3,128,128]) tuple(add.1)
}
)";
  auto module = ParseAndReturnVerifiedModule(hlo_string).ValueOrDie();
  ConditionalCodeMotion pass(true, true);
  ASSERT_TRUE(pass.Run(&*module).ValueOrDie());
  const HloInstruction* conditional =
      FindInstruction(module.get(), "conditional");
  CHECK(conditional != nullptr);
  const HloComputation* on_true = conditional->branch_computation(0);
  ASSERT_EQ(on_true->instruction_count(), 5);
  const HloComputation* on_false = conditional->branch_computation(1);
  ASSERT_EQ(on_false->instruction_count(), 5);

  // Checks if conditional shape has changed.
  ASSERT_TRUE(ShapeUtil::Compatible(
      conditional->shape(), ShapeUtil::MakeTupleShape({ShapeUtil::MakeShape(
                                BF16, {3, 3, 128, 128})})));
  HloInstruction* root = module->entry_computation()->root_instruction();
  EXPECT_THAT(
      root,
      AllOf(op::Tuple(op::Add(
          op::Convert(op::AllReduce(op::GetTupleElement(op::Conditional()))),
          op::Convert(
              op::AllReduce(op::GetTupleElement(op::Conditional())))))));
}

TEST_F(ConditionalCodeMotionTest, MovePowOpIn) {
  absl::string_view hlo_string =
      R"(
HloModule RemoveIdenticalInstruction

on_true {
  arg_tuple.1 = (f32[10]) parameter(0)
  get-tuple-element.1 = f32[10] get-tuple-element(arg_tuple.1), index=0
  add.1 = f32[10] add(get-tuple-element.1, get-tuple-element.1)
  ROOT tuple.3 = (f32[10]) tuple(add.1)
}

on_false {
  arg_tuple.2 = (f32[10]) parameter(0)
  get-tuple-element.2 = f32[10] get-tuple-element(arg_tuple.2), index=0
  mul.1 = f32[10] multiply(get-tuple-element.2, get-tuple-element.2)
  ROOT tuple.4 = (f32[10]) tuple(mul.1)
}

ENTRY main {
  pred.1 = pred[] parameter(0)
  tuple.1 = (f32[10]) parameter(1)
  tuple.2 = (f32[10]) parameter(2)
  conditional = (f32[10])
    conditional(pred.1, tuple.1, tuple.2), true_computation=on_true,
    false_computation=on_false
  get-first-index = f32[10] get-tuple-element(conditional), index=0
  ROOT pow.1 = f32[10] power(get-first-index, get-first-index)
}
)";
  auto module = ParseAndReturnVerifiedModule(hlo_string).ValueOrDie();
  ConditionalCodeMotion pass(true, true);
  ASSERT_TRUE(pass.Run(&*module).ValueOrDie());
  const HloInstruction* conditional =
      FindInstruction(module.get(), "conditional");
  const HloComputation* on_true = conditional->branch_computation(0);
  ASSERT_EQ(on_true->instruction_count(), 5);
  const HloComputation* on_false = conditional->branch_computation(1);
  ASSERT_EQ(on_false->instruction_count(), 5);

  HloInstruction* root = module->entry_computation()->root_instruction();
  EXPECT_THAT(root, AllOf(op::GetTupleElement(op::Conditional())));
}

TEST_F(ConditionalCodeMotionTest, MoveInWithMultipleGTE) {
  absl::string_view hlo_string =
      R"(
HloModule RemoveIdenticalInstruction

on_true {
  arg_tuple.1 = (f32[10]) parameter(0)
  get-tuple-element.1 = f32[10] get-tuple-element(arg_tuple.1), index=0
  add.1 = f32[10] add(get-tuple-element.1, get-tuple-element.1)
  ROOT tuple.3 = (f32[10]) tuple(add.1)
}

on_false {
  arg_tuple.2 = (f32[10]) parameter(0)
  get-tuple-element.2 = f32[10] get-tuple-element(arg_tuple.2), index=0
  mul.1 = f32[10] multiply(get-tuple-element.2, get-tuple-element.2)
  ROOT tuple.4 = (f32[10]) tuple(mul.1)
}

ENTRY main {
  pred.1 = pred[] parameter(0)
  tuple.1 = (f32[10]) parameter(1)
  tuple.2 = (f32[10]) parameter(2)
  conditional = (f32[10])
    conditional(pred.1, tuple.1, tuple.2), true_computation=on_true,
    false_computation=on_false
  get-first-index = f32[10] get-tuple-element(conditional), index=0
  get-first-index.2 = f32[10] get-tuple-element(conditional), index=0
  pow.1 = f32[10] power(get-first-index, get-first-index.2)
  ROOT tuple.3 = (f32[10], f32[10]) tuple(pow.1, get-first-index.2)
}
)";
  auto module = ParseAndReturnVerifiedModule(hlo_string).ValueOrDie();
  ConditionalCodeMotion pass(true, true);
  ASSERT_TRUE(pass.Run(&*module).ValueOrDie());
  HloInstruction* root = module->entry_computation()->root_instruction();
  EXPECT_THAT(root, op::Tuple(op::GetTupleElement(op::Conditional()),
                              op::GetTupleElement(op::Conditional())));
}

TEST_F(ConditionalCodeMotionTest, MoveOutWithSharedBranch) {
  absl::string_view hlo_string =
      R"(
HloModule RemoveIdenticalInstruction

branch {
  arg_tuple.1 = (f32[10]) parameter(0)
  get-tuple-element.1 = f32[10] get-tuple-element(arg_tuple.1), index=0
  add.1 = f32[10] add(get-tuple-element.1, get-tuple-element.1)
  ROOT tuple.3 = (f32[10]) tuple(add.1)
}

ENTRY main {
  pred.1 = pred[] parameter(0)
  tuple.1 = (f32[10]) parameter(1)
  tuple.2 = (f32[10]) parameter(2)
  conditional = (f32[10])
    conditional(pred.1, tuple.1, tuple.2), true_computation=branch,
    false_computation=branch
  get-first-index = f32[10] get-tuple-element(conditional), index=0
  ROOT pow.1 = f32[10] power(get-first-index, get-first-index)
}
)";
  auto module = ParseAndReturnVerifiedModule(hlo_string).ValueOrDie();
  ConditionalCodeMotion pass(true, true);
  ASSERT_TRUE(pass.Run(&*module).ValueOrDie());
  const HloInstruction* conditional =
      FindInstruction(module.get(), "conditional");
  const HloComputation* on_true = conditional->branch_computation(0);
  ASSERT_EQ(on_true->instruction_count(), 1);
  const HloComputation* on_false = conditional->branch_computation(1);
  ASSERT_EQ(on_false->instruction_count(), 1);

  HloInstruction* root = module->entry_computation()->root_instruction();
  EXPECT_THAT(
      root, AllOf(op::Power(op::Add(op::GetTupleElement(op::Conditional()),
                                    op::GetTupleElement(op::Conditional())),
                            op::Add(op::GetTupleElement(op::Conditional()),
                                    op::GetTupleElement(op::Conditional())))));
}

TEST_F(ConditionalCodeMotionTest, MovePowInWithNonTupleRoot) {
  absl::string_view hlo_string =
      R"(
HloModule RemoveIdenticalInstruction

branch {
  arg_tuple.1 = (f32[10]) parameter(0)
  get-tuple-element.1 = f32[10] get-tuple-element(arg_tuple.1), index=0
  ROOT add.1 = f32[10] add(get-tuple-element.1, get-tuple-element.1)
}

ENTRY main {
  pred.1 = pred[] parameter(0)
  tuple.1 = (f32[10]) parameter(1)
  tuple.2 = (f32[10]) parameter(2)
  conditional = f32[10]
    conditional(pred.1, tuple.1, tuple.2), true_computation=branch,
    false_computation=branch
  ROOT pow.1 = f32[10] power(conditional, conditional)
}
)";
  auto module = ParseAndReturnVerifiedModule(hlo_string).ValueOrDie();
  ConditionalCodeMotion pass(true, true);
  ASSERT_TRUE(pass.Run(&*module).ValueOrDie());
  const HloInstruction* conditional =
      FindInstruction(module.get(), "conditional");
  const HloComputation* on_true = conditional->branch_computation(0);
  ASSERT_EQ(on_true->instruction_count(), 5);
  const HloComputation* on_false = conditional->branch_computation(1);
  ASSERT_EQ(on_false->instruction_count(), 5);

  HloInstruction* root = module->entry_computation()->root_instruction();
  EXPECT_THAT(root, AllOf(op::GetTupleElement(op::Conditional())));
}

TEST_F(ConditionalCodeMotionTest, MovePowInWithEmptyBranch) {
  absl::string_view hlo_string =
      R"(
HloModule RemoveIdenticalInstruction

branch1 {
  arg_tuple.1 = (f32[10]) parameter(0)
  get-tuple-element.1 = f32[10] get-tuple-element(arg_tuple.1), index=0
  add.1 = f32[10] add(get-tuple-element.1, get-tuple-element.1)
  ROOT tuple.3 = (f32[10]) tuple(add.1)
}

branch2 {
  ROOT arg_tuple.1 = (f32[10]) parameter(0)
}

ENTRY main {
  pred.1 = pred[] parameter(0)
  tuple.1 = (f32[10]) parameter(1)
  tuple.2 = (f32[10]) parameter(2)
  conditional = (f32[10])
    conditional(pred.1, tuple.1, tuple.2), true_computation=branch1,
    false_computation=branch2
  get-first-index = f32[10] get-tuple-element(conditional), index=0
  ROOT pow.1 = f32[10] power(get-first-index, get-first-index)
}
)";
  auto module = ParseAndReturnVerifiedModule(hlo_string).ValueOrDie();
  ConditionalCodeMotion pass(true, true);
  ASSERT_TRUE(pass.Run(&*module).ValueOrDie());
  const HloInstruction* conditional =
      FindInstruction(module.get(), "conditional");
  const HloComputation* on_true = conditional->branch_computation(0);
  ASSERT_EQ(on_true->instruction_count(), 5);
  const HloComputation* on_false = conditional->branch_computation(1);
  ASSERT_EQ(on_false->instruction_count(), 4);

  HloInstruction* root = module->entry_computation()->root_instruction();
  EXPECT_THAT(root, AllOf(op::GetTupleElement(op::Conditional())));
}

TEST_F(ConditionalCodeMotionTest, MovePowInWithNonTupleParameter) {
  absl::string_view hlo_string =
      R"(
HloModule RemoveIdenticalInstruction

branch {
  arg.1 = f32[10] parameter(0)
  ROOT add.1 = f32[10] add(arg.1, arg.1)
}

ENTRY main {
  pred.1 = pred[] parameter(0)
  tuple.1 = f32[10] parameter(1)
  tuple.2 = f32[10] parameter(2)
  conditional = f32[10]
    conditional(pred.1, tuple.1, tuple.2), true_computation=branch,
    false_computation=branch
  ROOT pow.1 = f32[10] power(conditional, conditional)
}
)";
  auto module = ParseAndReturnVerifiedModule(hlo_string).ValueOrDie();
  ConditionalCodeMotion pass(true, true);
  ASSERT_TRUE(pass.Run(&*module).ValueOrDie());
  const HloInstruction* conditional =
      FindInstruction(module.get(), "conditional");
  const HloComputation* on_true = conditional->branch_computation(0);
  ASSERT_EQ(on_true->instruction_count(), 4);
  const HloComputation* on_false = conditional->branch_computation(1);
  ASSERT_EQ(on_false->instruction_count(), 4);

  HloInstruction* root = module->entry_computation()->root_instruction();
  EXPECT_THAT(root, AllOf(op::GetTupleElement(op::Conditional())));
}

TEST_F(ConditionalCodeMotionTest, MoveCopyInBranch) {
  absl::string_view hlo_string =
      R"(
HloModule RemoveIdenticalInstruction

branch1 {
  arg_tuple.1 = (s32[], f32[10,3]{0,1}) parameter(0)
  constant.1 = s32[] constant(4)
  get-tuple-element.1 = s32[] get-tuple-element(arg_tuple.1), index=0
  add.1 = s32[] add(get-tuple-element.1, constant.1)
  get-tuple-element.2 = f32[10,3]{0,1} get-tuple-element(arg_tuple.1), index=1
  slice.1 = f32[4,3]{0,1} slice(get-tuple-element.2),
   slice={[0:4:1], [0:3:1]}
  constant.2 = f32[] constant(0.0)
  ROOT tuple.1 = (f32[4,3]{0,1}, s32[],f32[]) tuple(slice.1, add.1, constant.2)
}

branch2 {
  arg_tuple.2 = (s32[], f32[4,3]{1,0}) parameter(0)
  get-tuple-element.3 = s32[] get-tuple-element(arg_tuple.2), index=0
  copy.1 = s32[] copy(get-tuple-element.3)
  get-tuple-element.4 = f32[4,3]{1,0} get-tuple-element(arg_tuple.2), index=1
  copy.2 = f32[4,3]{0,1} copy(get-tuple-element.4)
  constant.2 = f32[] constant(0.0)
  ROOT tuple.2 = (f32[4,3]{0,1}, s32[], f32[]) tuple(copy.2, copy.1, constant.2)
}

ENTRY main {
  pred.1 = pred[] parameter(0)
  tuple.3 = (s32[], f32[10,3]{0,1}) parameter(1)
  tuple.4 = (s32[], f32[4,3]{1,0}) parameter(2)
  conditional = (f32[4,3]{0,1}, s32[], f32[])
    conditional(pred.1, tuple.3, tuple.4), true_computation=branch1,
    false_computation=branch2
  get-zero-index = f32[4,3]{0,1} get-tuple-element(conditional), index=0
  get-first-index = s32[] get-tuple-element(conditional), index=1
  get-second-index = f32[] get-tuple-element(conditional), index=2
  copy.3 = f32[4,3]{1,0} copy(get-zero-index)
  ROOT tuple.5 = (f32[4,3]{0,1}, s32[], f32[]) tuple(copy.3, get-first-index,
                 get-second-index)
}
)";
  auto module = ParseAndReturnVerifiedModule(hlo_string).ValueOrDie();
  ConditionalCodeMotion pass(true, true);
  ASSERT_TRUE(pass.Run(&*module).ValueOrDie());
  VLOG(1) << module->ToString();

  const HloInstruction* conditional =
      FindInstruction(module.get(), "conditional");
  const HloComputation* on_true = conditional->branch_computation(0);
  ASSERT_EQ(on_true->instruction_count(), 9);
  const HloComputation* on_false = conditional->branch_computation(1);
  ASSERT_EQ(on_false->instruction_count(), 8);
  HloInstruction* root = module->entry_computation()->root_instruction();
  EXPECT_THAT(root,
              AllOf(op::Tuple(op::GetTupleElement(op::Conditional(), 2),
                              op::GetTupleElement(op::Conditional(), 0),
                              op::GetTupleElement(op::Conditional(), 1))));
}

TEST_F(ConditionalCodeMotionTest, MoveReplicatedTupleEntryOut) {
  absl::string_view hlo_string =
      R"(
HloModule RemoveIdenticalInstruction

%add.64 (x.139: bf16[], y.139: bf16[]) -> bf16[] {
  %x.139 = bf16[]{:T(512)} parameter(0)
  %y.139 = bf16[]{:T(512)} parameter(1)
  ROOT %add.44073 = bf16[]{:T(512)} add(bf16[]{:T(512)} %x.139, bf16[]{:T(512)} %y.139)
}

%add.181 (x.256: bf16[], y.256: bf16[]) -> bf16[] {
  %x.256 = bf16[]{:T(512)} parameter(0)
  %y.256 = bf16[]{:T(512)} parameter(1)
  ROOT %add.44842 = bf16[]{:T(512)} add(bf16[]{:T(512)} %x.256, bf16[]{:T(512)} %y.256)
}

on_true {
  arg_tuple.1 = (bf16[2,54,168,128], bf16[2,52,168,128]) parameter(0)
  get-tuple-element.11 = bf16[2,54,168,128] get-tuple-element(arg_tuple.1), index=0
  get-tuple-element.12 = bf16[2,52,168,128] get-tuple-element(arg_tuple.1), index=1
  convolution.1 = bf16[3,3,128,128] convolution(bf16[2,54,168,128]
    get-tuple-element.11, bf16[2,52,168,128]
    get-tuple-element.12), window={size=52x168 pad=0_0x1_1},
    dim_labels=f01b_i01o->01bf
  all-reduce.1 = bf16[3,3,128,128]
    all-reduce(bf16[3,3,128,128] %convolution.1),
    channel_id=188, replica_groups={{0,1}}, use_global_device_ids=true,
    to_apply=%add.64
  convert.1 = f32[3,3,128,128] convert(bf16[3,3,128,128] %all-reduce.1)
  all-reduce.3 = bf16[3,3,128,128]
    all-reduce(bf16[3,3,128,128] %convolution.1),
    channel_id=188, replica_groups={{0,1}}, use_global_device_ids=true,
    to_apply=%add.64
  convert.3 = f32[3,3,128,128] convert(bf16[3,3,128,128] %all-reduce.3)
  ROOT tuple.1 = (f32[3,3,128,128], f32[3,3,128,128]) tuple(convert.1, convert.3)
}

on_false {
  arg_tuple.2 = (bf16[2,86,104,128], bf16[2,84,104,128]) parameter(0)
  get-tuple-element.21 = bf16[2,86,104,128]
    get-tuple-element(arg_tuple.2), index=0
  get-tuple-element.22 = bf16[2,84,104,128]
    get-tuple-element(arg_tuple.2), index=1
  convolution.2 = bf16[3,3,128,128]
    convolution(bf16[2,86,104,128] get-tuple-element.21, bf16[2,84,104,128]
    get-tuple-element.22), window={size=84x104 pad=0_0x1_1},
    dim_labels=f01b_i01o->01bf
  all-reduce.2 = bf16[3,3,128,128]
    all-reduce(bf16[3,3,128,128] %convolution.2),
    channel_id=485, replica_groups={{0,1}}, use_global_device_ids=true,
    to_apply=%add.181
  convert.2 = f32[3,3,128,128]
    convert(bf16[3,3,128,128] %all-reduce.2)
  ROOT tuple.2 = (f32[3,3,128,128], f32[3,3,128,128]) tuple(convert.2, convert.2)
}

ENTRY main {
  pred.1 = pred[] parameter(0)
  arg_tuple.3 = (bf16[2,54,168,128], bf16[2,52,168,128]) parameter(1)
  arg_tuple.4 = (bf16[2,86,104,128], bf16[2,84,104,128]) parameter(2)
  conditional = (f32[3,3,128,128], f32[3,3,128,128])
    conditional(pred.1, arg_tuple.3, arg_tuple.4), true_computation=on_true,
    false_computation=on_false
  get-first-index = f32[3,3,128,128]
    get-tuple-element(conditional), index=0
  add.1 = f32[3,3,128,128] add(f32[3,3,128,128] get-first-index, f32[3,3,128,128] get-first-index)
  ROOT result = (f32[3,3,128,128]) tuple(add.1)
}
)";
  auto module = ParseAndReturnVerifiedModule(hlo_string).ValueOrDie();
  ConditionalCodeMotion pass(true, true);
  ASSERT_TRUE(pass.Run(&*module).ValueOrDie());
  const HloInstruction* conditional =
      FindInstruction(module.get(), "conditional");
  const HloComputation* on_true = conditional->branch_computation(0);
  ASSERT_EQ(on_true->instruction_count(), 5);
  const HloComputation* on_false = conditional->branch_computation(1);
  ASSERT_EQ(on_false->instruction_count(), 5);

  // Checks if conditional shape has changed.
  ASSERT_TRUE(ShapeUtil::Compatible(
      conditional->shape(), ShapeUtil::MakeTupleShape({ShapeUtil::MakeShape(
                                BF16, {3, 3, 128, 128})})));
  HloInstruction* root = module->entry_computation()->root_instruction();
  EXPECT_THAT(
      root,
      AllOf(op::Tuple(op::Add(
          op::Convert(op::AllReduce(op::GetTupleElement(op::Conditional()))),
          op::Convert(
              op::AllReduce(op::GetTupleElement(op::Conditional())))))));
}

TEST_F(ConditionalCodeMotionTest, DoNotMoveWithExtraOperand) {
  absl::string_view hlo_string =
      R"(
HloModule RemoveIdenticalInstruction

branch {
  arg.1 = f32[10] parameter(0)
  ROOT add.1 = f32[10] add(arg.1, arg.1)
}

ENTRY main {
  pred.1 = pred[] parameter(0)
  tuple.1 = f32[10] parameter(1)
  tuple.2 = f32[10] parameter(2)
  conditional = f32[10]
    conditional(pred.1, tuple.1, tuple.2), true_computation=branch,
    false_computation=branch
  ROOT pow.1 = f32[10] power(conditional, tuple.2)
}
)";
  auto module = ParseAndReturnVerifiedModule(hlo_string).ValueOrDie();
  ConditionalCodeMotion pass(true, true);
  ASSERT_FALSE(pass.Run(&*module).ValueOrDie());
}

TEST_F(ConditionalCodeMotionTest, MultipleIndependentMoveIns) {
  absl::string_view hlo_string =
      R"(
HloModule FromNMT

%add.31755 (x.139: f32[], y.139: bf16[]) -> bf16[] {
  %x.139 = bf16[]{:T(512)} parameter(0)
  %y.139 = bf16[]{:T(512)} parameter(1)
  ROOT %add.44073 = bf16[]{:T(512)} add(bf16[]{:T(512)} %x.139, bf16[]{:T(512)} %y.139)
}

%nmt.1 {
  %wide_param.3 = (bf16[1024,4096]{1,0}, bf16[18,64,1024]{2,1,0}, s32[]) parameter(0)
  %get-tuple-element.16525 = bf16[1024,4096]{1,0} get-tuple-element((bf16[1024,4096]{1,0}, bf16[18,64,1024]{2,1,0}, s32[]) %wide_param.3), index=0
  %get-tuple-element.16527 = bf16[18,64,1024]{2,1,0} get-tuple-element((bf16[1024,4096]{1,0}, bf16[18,64,1024]{2,1,0}, s32[]) %wide_param.3), index=1
  %get-tuple-element.16588 = s32[] get-tuple-element((bf16[1024,4096]{1,0}, bf16[18,64,1024]{2,1,0}, s32[]) %wide_param.3), index=2
  %add.3764 = s32[] add(s32[] %get-tuple-element.16588, s32[] %get-tuple-element.16588), metadata={op_type="Sub" op_name="sub"}
  %reshape.9821 = s32[1]{0} reshape(s32[] %add.3764)
  %reshape.9822 = s32[] reshape(s32[1]{0} %reshape.9821)
  %constant.13127 = s32[] constant(0)
  %dynamic-slice.1245 = bf16[1,64,1024]{2,1,0} dynamic-slice(bf16[18,64,1024]{2,1,0} %get-tuple-element.16527, s32[] %reshape.9822, s32[] %constant.13127, s32[] %constant.13127), dynamic_slice_sizes={1,64,1024}
  %reshape.9825 = bf16[64,1024]{1,0} reshape(bf16[1,64,1024]{2,1,0} %dynamic-slice.1245), metadata={op_type="GatherV2" op_name="GatherV2"}
  %logistic.814 = bf16[64,1024]{1,0} logistic(bf16[64,1024]{1,0} %reshape.9825), metadata={op_type="Sigmoid" op_name="Sigmoid"}
  %multiply.4890 = bf16[64,1024]{1,0} multiply(bf16[64,1024]{1,0} %reshape.9825, bf16[64,1024]{1,0} %logistic.814), metadata={op_type="Mul" op_name="mul"}
  %tanh.573 = bf16[64,1024]{1,0} tanh(bf16[64,1024]{1,0} %reshape.9825), metadata={op_type="Tanh" op_name="Tanh"}
  %multiply.4891 = bf16[64,1024]{1,0} multiply(bf16[64,1024]{1,0} %logistic.814, bf16[64,1024]{1,0} %tanh.573), metadata={op_type="Mul" op_name="mul_1"}
  %add.3766 = bf16[64,1024]{1,0} add(bf16[64,1024]{1,0} %multiply.4890, bf16[64,1024]{1,0} %multiply.4891), metadata={op_type="AddV2" op_name="add_1"}
  %multiply.4894 = bf16[64,1024]{1,0} multiply(bf16[64,1024]{1,0} %add.3766, bf16[64,1024]{1,0} %logistic.814), metadata={op_type="Mul" op_name="gradients_1/mul_grad/Mul"}
  %constant.10568 = bf16[] constant(1), metadata={op_type="TanhGrad" op_name="gradients/Tanh_1_grad/TanhGrad"}
  %broadcast.7198 = bf16[64,1024]{1,0} broadcast(bf16[] %constant.10568), dimensions={}, metadata={op_type="TanhGrad" op_name="gradients/Tanh_1_grad/TanhGrad"}
  %multiply.4896 = bf16[64,1024]{1,0} multiply(bf16[64,1024]{1,0} %tanh.573, bf16[64,1024]{1,0} %tanh.573), metadata={op_type="TanhGrad" op_name="gradients/Tanh_1_grad/TanhGrad"}
  %constant.10571 = bf16[] constant(1), metadata={op_type="SigmoidGrad" op_name="gradients/Sigmoid_grad/SigmoidGrad"}
  %broadcast.7201 = bf16[64,1024]{1,0} broadcast(bf16[] %constant.10571), dimensions={}, metadata={op_type="SigmoidGrad" op_name="gradients/Sigmoid_grad/SigmoidGrad"}
  %subtract.1702 = bf16[64,1024]{1,0} subtract(bf16[64,1024]{1,0} %broadcast.7201, bf16[64,1024]{1,0} %logistic.814), metadata={op_type="SigmoidGrad" op_name="gradients/Sigmoid_grad/SigmoidGrad"}
  %multiply.4907 = bf16[64,1024]{1,0} multiply(bf16[64,1024]{1,0} %tanh.573, bf16[64,1024]{1,0} %add.3766), metadata={op_type="Mul" op_name="gradients/mul_2_grad/Mul_1"}
  %multiply.4908 = bf16[64,1024]{1,0} multiply(bf16[64,1024]{1,0} %multiply.4907, bf16[64,1024]{1,0} %logistic.814), metadata={op_type="SigmoidGrad" op_name="gradients/Sigmoid_2_grad/SigmoidGrad"}
  %dot.781 = bf16[64,4096]{1,0} dot(bf16[64,1024]{1,0} %multiply.4908, bf16[1024,4096]{1,0} %get-tuple-element.16525), lhs_contracting_dims={1}, rhs_contracting_dims={0}, metadata={op_type="MatMul" op_name="MatMul"}
  ROOT %tuple.3200 = (bf16[64,1024]{1,0}, bf16[64,4096]{1,0}, s32[]) tuple(bf16[64,1024]{1,0} %multiply.4894, bf16[64,4096]{1,0} %dot.781, s32[] %reshape.9822)
  }
ENTRY main {
  pred.1 = pred[] parameter(0)
  arg_tuple.3 = (bf16[1024,4096]{1,0}, bf16[18,64,1024]{2,1,0}, s32[]) parameter(1)
  arg_tuple.4 = (bf16[1024,4096]{1,0}, bf16[18,64,1024]{2,1,0}, s32[]) parameter(2)
  %arg.2 = s32[] parameter(3)
  %conditional.3 = (bf16[64,1024]{1,0}, bf16[64,4096]{1,0}, s32[]) conditional(pred.1, arg_tuple.3, arg_tuple.4), true_computation=nmt.1, false_computation=nmt.1
  %get-tuple-element.15889 = bf16[64,1024]{1,0} get-tuple-element((bf16[64,1024]{1,0}, bf16[64,4096]{1,0}, s32[]) %conditional.3), index=0, metadata={op_type="Case" op_name="switch_case/indexed_case"}
  %multiply.4596 = bf16[64,1024]{1,0} multiply(bf16[64,1024]{1,0} %get-tuple-element.15889, bf16[64,1024]{1,0} %get-tuple-element.15889), metadata={op_type="L2Loss" op_name="global_norm/L2Loss"}
  %constant.10279 = bf16[] constant(0), metadata={op_type="L2Loss" op_name="global_norm/L2Loss"}
  %reduce.844 = bf16[] reduce(bf16[64,1024]{1,0} %multiply.4596, bf16[] %constant.10279), dimensions={0,1}, to_apply=%add.31755, metadata={op_type="L2Loss" op_name="global_norm/L2Loss"}
  %get-tuple-element.15890 = bf16[64,4096]{1,0} get-tuple-element((bf16[64,1024]{1,0}, bf16[64,4096]{1,0}, s32[]) %conditional.3), index=1, metadata={op_type="Case" op_name="switch_case/indexed_case"}
  %multiply.4597 = bf16[64,4096]{1,0} multiply(bf16[64,4096]{1,0} %get-tuple-element.15890, bf16[64,4096]{1,0} %get-tuple-element.15890), metadata={op_type="L2Loss" op_name="global_norm/L2Loss"}
  %constant.10280 = bf16[] constant(0), metadata={op_type="L2Loss" op_name="global_norm/L2Loss"}
  %reduce.845 = bf16[] reduce(bf16[64,4096]{1,0} %multiply.4597, bf16[] %constant.10280), dimensions={0,1}, to_apply=%add.31755, metadata={op_type="L2Loss" op_name="global_norm/L2Loss"}
  %multiply.4667 = bf16[] multiply(bf16[] %reduce.845, bf16[]{:T(128)} %reduce.844), metadata={op_type="L2Loss" op_name="global_norm/L2Loss"}
  ROOT %tuple.3200 = (bf16[], s32[]) tuple(%multiply.4667, s32[] %arg.2)
  }
)";
  auto module = ParseAndReturnVerifiedModule(hlo_string).ValueOrDie();
  ConditionalCodeMotion pass(true, true);
  ASSERT_TRUE(pass.Run(&*module).ValueOrDie());
  const HloInstruction* conditional =
      FindInstruction(module.get(), "conditional.3");
  CHECK(conditional != nullptr);
  const HloComputation* on_true = conditional->branch_computation(0);
  ASSERT_EQ(on_true->instruction_count(), 27);
  const HloComputation* on_false = conditional->branch_computation(1);
  ASSERT_EQ(on_false->instruction_count(), 27);

  HloInstruction* root = module->entry_computation()->root_instruction();
  EXPECT_THAT(root, AllOf(op::Tuple(op::GetTupleElement(op::Conditional()),
                                    op::Parameter())));
}

}  // namespace conditional_opt

}  // namespace xla
