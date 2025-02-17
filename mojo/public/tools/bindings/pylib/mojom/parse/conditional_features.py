# Copyright 2018 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Helpers for processing conditionally enabled features in a mojom."""

from . import ast
from ..error import Error

class EnableIfError(Error):
  """ Class for errors from ."""

  def __init__(self, filename, message, lineno=None):
    Error.__init__(self, filename, message, lineno=lineno, addenda=None)

def _IsEnabled(definition, enabled_features):
  """Returns true if a definition is enabled.

  A definition is enabled if it has no EnableIf attribute, or if the value of
  the EnableIf attribute is in enabled_features.
  """
  if not hasattr(definition, "attribute_list"):
    return True
  if not definition.attribute_list:
    return True

  already_defined = False
  for a in definition.attribute_list:
    if a.key == 'EnableIf':
      if already_defined:
        raise EnableIfError(definition.filename,
          "EnableIf attribute may only be defined once per field.",
          definition.lineno)
      already_defined = True

  for attribute in definition.attribute_list:
    if attribute.key == 'EnableIf' and attribute.value not in enabled_features:
      return False
  return True


def _FilterDisabledFromNodeList(node_list, enabled_features):
  if not node_list:
    return
  assert isinstance(node_list, ast.NodeListBase)
  node_list.items = [
      item for item in node_list.items if _IsEnabled(item, enabled_features)
  ]
  for item in node_list.items:
    _FilterDefinition(item, enabled_features)


def _FilterDefinition(definition, enabled_features):
  """Filters definitions with a body."""
  if isinstance(definition, ast.Enum):
    _FilterDisabledFromNodeList(definition.enum_value_list, enabled_features)
  elif isinstance(definition, ast.Interface):
    _FilterDisabledFromNodeList(definition.body, enabled_features)
  elif isinstance(definition, ast.Method):
    _FilterDisabledFromNodeList(definition.parameter_list, enabled_features)
    _FilterDisabledFromNodeList(definition.response_parameter_list,
                                enabled_features)
  elif isinstance(definition, ast.Struct):
    _FilterDisabledFromNodeList(definition.body, enabled_features)
  elif isinstance(definition, ast.Union):
    _FilterDisabledFromNodeList(definition.body, enabled_features)


def RemoveDisabledDefinitions(mojom, enabled_features):
  """Removes conditionally disabled definitions from a Mojom node."""
  mojom.definition_list = [
      definition for definition in mojom.definition_list
          if _IsEnabled(definition, enabled_features)
  ]
  for definition in mojom.definition_list:
    _FilterDefinition(definition, enabled_features)
