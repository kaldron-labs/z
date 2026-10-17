// Copyright 2026 Huw Rogers. Licensed under the MIT license (see LICENSE).

// Independent protobuf peer for zpbgotest. Schemas come from native Zpb
// metadata; decoding, construction and framing use canonical Go protobuf.
package main

import (
	"bufio"
	"bytes"
	"errors"
	"flag"
	"fmt"
	"io"
	"os"

	"google.golang.org/protobuf/encoding/protodelim"
	"google.golang.org/protobuf/encoding/prototext"
	"google.golang.org/protobuf/proto"
	"google.golang.org/protobuf/reflect/protodesc"
	"google.golang.org/protobuf/reflect/protoreflect"
	"google.golang.org/protobuf/types/descriptorpb"
	"google.golang.org/protobuf/types/dynamicpb"
)

// Bound test input well above the largest fixture (about 16 KiB).
const maxMessage = 1 << 20

func exchange(schema, kind, mode, input, output string) error {
	encoded, err := os.ReadFile(schema)
	if err != nil {
		return err
	}
	set := new(descriptorpb.FileDescriptorSet)
	if err := proto.Unmarshal(encoded, set); err != nil {
		return err
	}
	files, err := protodesc.NewFiles(set)
	if err != nil {
		return err
	}
	var name protoreflect.FullName
	var lengths []int
	switch kind {
	case "row":
		name = "fixture.Row"
		lengths = []int{0}
	case "blob":
		name = "fixture.Blob"
		lengths = []int{126}
		if mode == "delimited" {
			lengths = []int{0, 125, 126, 16380, 16381}
		}
	default:
		return fmt.Errorf("unknown fixture %q", kind)
	}
	desc, err := files.FindDescriptorByName(name)
	if err != nil {
		return err
	}
	md, ok := desc.(protoreflect.MessageDescriptor)
	if !ok {
		return fmt.Errorf("%s is not a message", name)
	}
	data, err := os.ReadFile(input)
	if err != nil {
		return err
	}
	reader := bufio.NewReader(bytes.NewReader(data))
	var result bytes.Buffer
	for i, length := range lengths {
		message := dynamicpb.NewMessage(md)
		switch mode {
		case "raw":
			err = proto.Unmarshal(data, message)
		case "delimited":
			err = (protodelim.UnmarshalOptions{MaxSize: maxMessage}).UnmarshalFrom(reader, message)
		default:
			return fmt.Errorf("unknown framing %q", mode)
		}
		if err != nil {
			return fmt.Errorf("message %d: %w", i, err)
		}
		expected := dynamicpb.NewMessage(md)
		if kind == "row" {
			const text = `id: 18446744073709551614 delta: -12345 ratio: 1.25
				text: "native" bytes: "\000\377"
				samples: [-1, 0, 12345] flags: [true, false, true]
				child: {value: 42}
				labels: {key: "a" value: -7} labels: {key: "b" value: 9}
				first: {value: 99}`
			if err := prototext.Unmarshal([]byte(text), expected); err != nil {
				return err
			}
			if !bytes.Equal(message.GetUnknown(), []byte{0x98, 0x06, 0x07}) {
				return errors.New("unknown field 99 was not retained")
			}
			message.SetUnknown(nil)
		} else if length != 0 {
			expected.Set(md.Fields().ByNumber(1), protoreflect.ValueOfBytes(bytes.Repeat([]byte{0x5a}, length)))
		}
		if !proto.Equal(message, expected) {
			return fmt.Errorf("message %d does not match independent Go fixture", i)
		}
		// Construct the response independently rather than echoing input bytes.
		if kind == "row" {
			expected.Set(md.Fields().ByNumber(1), protoreflect.ValueOfUint64(42))
			expected.Set(md.Fields().ByNumber(4), protoreflect.ValueOfString("go"))
			second := md.Fields().ByNumber(11)
			other := dynamicpb.NewMessage(second.Message())
			other.Set(other.Descriptor().Fields().ByNumber(1), protoreflect.ValueOfString("go"))
			expected.Set(second, protoreflect.ValueOfMessage(other))
			expected.SetUnknown([]byte{0xa0, 0x06, 0x09}) // unknown field 100
		}
		if mode == "delimited" {
			_, err = protodelim.MarshalTo(&result, expected)
		} else {
			var wire []byte
			wire, err = proto.Marshal(expected)
			result.Write(wire)
		}
		if err != nil {
			return err
		}
	}
	if mode == "delimited" {
		if _, err := reader.ReadByte(); !errors.Is(err, io.EOF) {
			return errors.New("trailing bytes after message sequence")
		}
	}
	return os.WriteFile(output, result.Bytes(), 0600)
}

func main() {
	schema := flag.String("schema", "", "native-exported FileDescriptorSet")
	kind := flag.String("kind", "row", "row or blob fixture")
	mode := flag.String("mode", "raw", "raw or delimited framing")
	input := flag.String("input", "", "native input file")
	output := flag.String("output", "", "Go response file")
	flag.Parse()
	if *schema == "" || *input == "" || *output == "" || flag.NArg() != 0 {
		flag.Usage()
		os.Exit(2)
	}
	if err := exchange(*schema, *kind, *mode, *input, *output); err != nil {
		fmt.Fprintln(os.Stderr, err)
		os.Exit(1)
	}
}
