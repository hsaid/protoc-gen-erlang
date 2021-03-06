// Erlang pluging to Protocol Buffers
// Copyright 2011 Tensor Wrench LLC.
// https://github.com/TensorWrench/protoc-gen-erlang

// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
// notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
// copyright notice, this list of conditions and the following disclaimer
// in the documentation and/or other materials provided with the
// distribution.
//     * Neither the name of Google Inc. nor the names of its
// contributors may be used to endorse or promote products derived from
// this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#include "ErlangGenerator.h"
/**
 * notable behaviors:
 * repeated string or bytes that are empty-- [<<>>,<<>>]  encode as a list of zero byte strings/bins
 * packable types encode as packed regardless of the attribute
 * TODO: check that a basic field defined multiple times correctly creates an array, since roundtrip tests only exercise the packed array branch
 */


namespace google {
namespace protobuf {
namespace compiler {
namespace erlang {

using google::protobuf::FileDescriptor;
using google::protobuf::FieldDescriptor;
using google::protobuf::Descriptor;
using google::protobuf::io::ZeroCopyOutputStream;
using google::protobuf::io::Printer;

/**
 * Creates the exports for enum translation.  Calling functions handle the trailing "," if necessary.
 */
void ErlangGenerator::export_for_enum(Printer& out, const EnumDescriptor* d) const
{
  out.Print("  $to$/1,$from$/1","to",to_enum_name(d),"from",from_enum_name(d));
}

/*
 * Exports all of the encode/decode pairs for the messages and nested messages
 * Calling functions handle the trailing "," if necessary.
 */
void ErlangGenerator::export_for_message(Printer& out, const Descriptor* d) const
{
  for(int i=0; i< d->nested_type_count();++i)
  {
    export_for_message(out,d->nested_type(i));
    out.PrintRaw(",\n");
  }
  for(int i=0; i < d->enum_type_count();++i)
  {
    export_for_enum(out,d->enum_type(i));
    out.PrintRaw(",\n");
  }
  out.Print("  $encode$/1,$decode$/1","encode",encode_name(d),"decode",decode_name(d));
}

/*
 * Creates the function clauses on the callback to protocol_buffers:decode/3.
 * Calling functions handle the trailing ";" if necessary.
 */
void ErlangGenerator::field_to_decode_function(Printer &out, const FieldDescriptor* field) const
{  std::map<string,string> vars;
  vars["id"]=int_to_string(field->number());
  vars["rec"]=to_atom(normalized_scope(field->containing_type()));
  vars["field"] = to_atom(field->name());
  vars["type"]=string(kTypeToName[field->type()]);

  switch (field->type()) {
    case FieldDescriptor::TYPE_STRING:
    case FieldDescriptor::TYPE_BYTES:
      // No such thing as a packed string/bytes, so we just append/replace multiple instances.
      if(field->is_repeated())
        out.Print(vars,"($id$,Val,#$rec${$field$=F}=Rec) when is_list(F) -> Rec#$rec${$field$ = Rec#$rec$.$field$ ++ [protocol_buffers:cast($type$,Val)]}\n");
      else
        out.Print(vars,"($id$,Val,Rec) -> Rec#$rec${$field$ = protocol_buffers:cast($type$,Val)}");
      break;
    case FieldDescriptor::TYPE_MESSAGE:
      // No such thing as a packed series of messages, so just append/replace multiple encounters.
      vars["decode"]=decode_impl_name(field->message_type());
      if(field->is_repeated())
        out.Print(vars,"($id$,{length_encoded,Bin},#$rec${$field$=F}=Rec) when is_list(F) -> Rec#$rec${$field$ = Rec#$rec$.$field$ ++ [$decode$(Bin)]}\n");
      else
        out.Print(vars,"($id$,{length_encoded,Bin},Rec) -> Rec#$rec${$field$ = $decode$(Bin)}");
      break;
    case FieldDescriptor::TYPE_ENUM:
      // As with integer types, but the additional step of to_enum()
      vars["to_enum"]=to_enum_name(field->enum_type());
      if(field->is_repeated())
        out.Print(vars,"($id$,{varint,Enum},#$rec${$field$=F}=Rec) when is_list(F) -> Rec#$rec${$field$=Rec#$rec$.$field$ ++ [$to_enum$(Enum)]}\n");
      else
        out.Print(vars,"($id$,{varint,Enum},Rec) -> Rec#$rec${$field$=$to_enum$(Enum)}");
      break;
    case FieldDescriptor::TYPE_GROUP:
      // not supported
      break;

    default:
      if(field->is_repeated())
      {
        // packed repeated returns array
        out.Print(vars,"        ($id$,{length_encoded,_}=Val,#$rec${$field$=F}=Rec) when is_list(F) -> Rec#$rec${$field$ = Rec#$rec$.$field$ ++ protocol_buffers:cast($type$,Val)};\n");
        // repeated that's not packed does not return an array
        out.Print(vars,"        ($id$,Val,#$rec${$field$=F}=Rec) when is_list(F) -> Rec#$rec${$field$ = Rec#$rec$.$field$ ++ [protocol_buffers:cast($type$,Val)]}\n");
      } else {
        out.Print(vars,"($id$,Val,Rec) -> Rec#$rec${$field$ = protocol_buffers:cast($type$,Val)}");
      }
  }
}

/*
 * Functions that translate from the atom to integer version of the enum.
 */
void ErlangGenerator::encode_decode_for_enum(Printer& out, const EnumDescriptor* d) const
{
  // to_enum
  for(int enumI=0;enumI < d->value_count();enumI++)
  {
    out.Print("$to_enum$($id$) -> $enum$;\n",
        "to_enum",to_enum_name(d),
        "id",int_to_string(d->value(enumI)->number()),
        "enum",to_atom(d->value(enumI)->name()));
  }
  out.Print("$to_enum$(undefined) -> undefined.\n\n","to_enum",to_enum_name(d));

  // from_enum
  for(int enumI=0;enumI < d->value_count();enumI++)
  {
    out.Print("$from_enum$($enum$) -> $id$;\n",
        "from_enum",from_enum_name(d),
        "id",int_to_string(d->value(enumI)->number()),
        "enum",to_atom(d->value(enumI)->name()));
  }
  out.Print("$from_enum$(undefined) -> undefined.\n\n","from_enum",from_enum_name(d));

}
/*
 * Creates the decoding and encoding aspects of the function.
 */
void ErlangGenerator::encode_decode_for_message(Printer& out, const Descriptor* d) const
{
  for(int i=0; i < d->enum_type_count();++i)
    encode_decode_for_enum(out,d->enum_type(i));

  for(int i=0; i< d->nested_type_count();++i)
    encode_decode_for_message(out,d->nested_type(i));

  // decode functions
  out.Print("$function$(B) ->\n"
            "  case $function_impl$(B) of\n"
            "    undefined -> #$msg${};\n"
            "    Any -> Any\n"
            "  end.\n\n"
            "$function_impl$(<<>>) -> undefined;\n"
            "$function_impl$(Binary) ->\n"
            "  protocol_buffers:decode(Binary,#$msg${},\n"
            "     fun",
              "function",decode_name(d),
              "function_impl",decode_impl_name(d),
              "msg",to_atom(normalized_scope(d)));

  for(int i=0; i< d->field_count();++i)
  {
    field_to_decode_function(out,d->field(i));
    if(i < d->field_count()-1)
    {
      out.PrintRaw(";\n        ");
    }
  }
  out.PrintRaw("\n      end).\n\n");

  // encode functions
  out.Print("$function$(undefined) -> undefined;\n"
            "$function$(R) when is_record(R,$rec$) ->\n"
            "  [\n",
              "function",encode_name(d),
              "rec",to_atom(normalized_scope(d)));
  for(int i=0; i< d->field_count();++i)
  {
    const FieldDescriptor* field=d->field(i);

    std::map<string,string> vars;
    vars["id"]=int_to_string(field->number());
    vars["rec"]=to_atom(normalized_scope(field->containing_type()));
    vars["field"] = to_atom(field->name());
    vars["type"]=string(kTypeToName[field->type()]);

    switch(field->type()) {
    case FieldDescriptor::TYPE_ENUM:
      vars["from_enum"]=from_enum_name(field->enum_type());
      if(field->is_repeated())
      {
        out.Print(vars,"    [protocol_buffers:encode($id$,int32,$from_enum$(X)) || X <- R#$rec$.$field$]");
      } else {
        out.Print(vars,"    protocol_buffers:encode($id$,int32,$from_enum$(R#$rec$.$field$))");
      }
      break;
    case FieldDescriptor::TYPE_MESSAGE:
      vars["encode"]=encode_name(field->message_type());
      if(field->is_repeated())
        out.Print(vars,"    [ protocol_buffers:encode($id$,length_encoded,$encode$(X)) || X <- R#$rec$.$field$]");
      else
        out.Print(vars,"    protocol_buffers:encode($id$,length_encoded,$encode$(R#$rec$.$field$))");
      break;
    case FieldDescriptor::TYPE_BYTES:
    case FieldDescriptor::TYPE_STRING:
      if(field->is_repeated())
        out.Print(vars,"    [ protocol_buffers:encode($id$,length_encoded,X) || X <- R#$rec$.$field$]");
      else
        out.Print(vars,"    protocol_buffers:encode($id$,length_encoded,R#$rec$.$field$)");
      break;

    default:
      out.Print(vars,"    protocol_buffers:encode($id$,$type$,R#$rec$.$field$)");
    }
    if(i<d->field_count()-1)
      out.PrintRaw(",\n");
  }
  out.PrintRaw("\n  ].\n\n");
}


void ErlangGenerator::generate_source(Printer& out, const FileDescriptor* file) const
{
  out.Print("-module($module$).\n"
            "-include(\"$module$.hrl\").\n\n"
             ,"module", module_name(file ));

  for (int i = 0; i < file->dependency_count(); ++i)
  {
      out.Print("-include(\"$module$.hrl\").\n", "module", module_name(file->dependency(i)));
  }
  out.Print("-export([\n");

  for(int i=0; i < file->enum_type_count();++i)
  {
    export_for_enum(out,file->enum_type(i));
    out.PrintRaw(",\n");
  }
  for(int i=0; i < file->message_type_count();++i) {
    export_for_message(out,file->message_type(i));
    if(i < file->message_type_count()-1)
      out.PrintRaw(",\n");
  }

  out.PrintRaw("]).\n\n");

  for(int i=0; i < file->enum_type_count();++i)
  {
    encode_decode_for_enum(out,file->enum_type(i));
  }

  for(int i=0; i < file->message_type_count();++i) {
    encode_decode_for_message(out,file->message_type(i));
  }
}
}}}} // namespace google::protobuf::compiler::erlang
