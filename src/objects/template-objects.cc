// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/objects/template-objects.h"

#include "src/base/functional.h"
#include "src/execution/isolate.h"
#include "src/heap/factory.h"
#include "src/objects/objects-inl.h"
#include "src/objects/property-descriptor.h"
#include "src/objects/template-objects-inl.h"

namespace v8 {
namespace internal {

// static
Handle<JSArray> TemplateObjectDescription::GetTemplateObject(
    Isolate* isolate, Handle<NativeContext> native_context,
    Handle<TemplateObjectDescription> description,
    Handle<SharedFunctionInfo> shared_info, int slot_id) {
  int function_literal_id = shared_info->function_literal_id();

  // Check the template weakmap to see if the template object already exists.
  Handle<EphemeronHashTable> template_weakmap;
  Handle<Script> script(Script::cast(shared_info->script(isolate)), isolate);
  int32_t hash =
      EphemeronHashTable::ShapeT::Hash(ReadOnlyRoots(isolate), script);
  Handle<HeapObject> cached_templates_head;
  MaybeHandle<CachedTemplateObject> existing_cached_template;

  if (native_context->template_weakmap().IsUndefined(isolate)) {
    template_weakmap = EphemeronHashTable::New(isolate, 1);
    cached_templates_head = isolate->factory()->the_hole_value();
  } else {
    DisallowGarbageCollection no_gc;
    ReadOnlyRoots roots(isolate);
    template_weakmap = handle(
        EphemeronHashTable::cast(native_context->template_weakmap()), isolate);
    cached_templates_head = handle(
        HeapObject::cast(template_weakmap->Lookup(isolate, script, hash)),
        isolate);

    HeapObject maybe_cached_template = *cached_templates_head;
    CachedTemplateObject previous_cached_template;
    while (!maybe_cached_template.IsTheHole(roots)) {
      CachedTemplateObject cached_template =
          CachedTemplateObject::cast(maybe_cached_template);
      if (cached_template.function_literal_id() == function_literal_id &&
          cached_template.slot_id() == slot_id) {
        HeapObject template_object;
        if (!cached_template.template_object(isolate).GetHeapObject(
                &template_object)) {
          // If the existing cached template is a cleared ref, update it
          // in-place.
          existing_cached_template = handle(cached_template, isolate);
          break;
        }
        return handle(JSArray::cast(template_object), isolate);
      }
      maybe_cached_template = cached_template.next();

      // Remove this entry from the list if it has a cleared object ref.
      if (cached_template.template_object(isolate).IsCleared()) {
        if (!previous_cached_template.is_null()) {
          previous_cached_template.set_next(maybe_cached_template);
        } else {
          DCHECK_EQ(cached_template, *cached_templates_head);
          cached_templates_head = handle(maybe_cached_template, isolate);
        }
      }
      previous_cached_template = cached_template;
    }
  }

  // Create the raw object from the {raw_strings}.
  Handle<FixedArray> raw_strings(description->raw_strings(), isolate);
  Handle<JSArray> raw_object = isolate->factory()->NewJSArrayWithElements(
      raw_strings, PACKED_ELEMENTS, raw_strings->length(),
      AllocationType::kOld);

  // Create the template object from the {cooked_strings}.
  Handle<FixedArray> cooked_strings(description->cooked_strings(), isolate);
  Handle<JSArray> template_object = isolate->factory()->NewJSArrayWithElements(
      cooked_strings, PACKED_ELEMENTS, cooked_strings->length(),
      AllocationType::kOld);

  // Freeze the {raw_object}.
  JSObject::SetIntegrityLevel(raw_object, FROZEN, kThrowOnError).ToChecked();

  // Install a "raw" data property for {raw_object} on {template_object}.
  PropertyDescriptor raw_desc;
  raw_desc.set_value(raw_object);
  raw_desc.set_configurable(false);
  raw_desc.set_enumerable(false);
  raw_desc.set_writable(false);
  JSArray::DefineOwnProperty(isolate, template_object,
                             isolate->factory()->raw_string(), &raw_desc,
                             Just(kThrowOnError))
      .ToChecked();

  // Freeze the {template_object} as well.
  JSObject::SetIntegrityLevel(template_object, FROZEN, kThrowOnError)
      .ToChecked();

  // Insert the template object into the template weakmap.
  Handle<CachedTemplateObject> cached_template;
  if (existing_cached_template.ToHandle(&cached_template)) {
    cached_template->set_template_object(
        HeapObjectReference::Weak(*template_object));
    // The existing cached template is already in the weakmap, so don't add it
    // again.
  } else {
    cached_template = isolate->factory()->NewCachedTemplateObject(
        function_literal_id, slot_id, cached_templates_head, template_object);

    // Add the new cached template to the weakmap as the new linked list head.
    template_weakmap = EphemeronHashTable::Put(isolate, template_weakmap,
                                               script, cached_template, hash);
    native_context->set_template_weakmap(*template_weakmap);
  }

  return template_object;
}

}  // namespace internal
}  // namespace v8
