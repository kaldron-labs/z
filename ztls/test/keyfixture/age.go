package main

import (
	"bytes"
	"encoding/base64"
	"strings"

	"filippo.io/age"
)

func encrypt(name string, recipient age.Recipient, plain []byte) {
	var buf bytes.Buffer
	w := must(age.Encrypt(&buf, recipient))
	_, err := w.Write(plain)
	check(err)
	check(w.Close())
	write(name+".age", buf.Bytes())
}

// Construct authenticated headers with the stanza arrangements exercised by the reader.
type stanzaList func([]byte) []*age.Stanza

func (f stanzaList) Wrap(key []byte) ([]*age.Stanza, error) { return f(key), nil }

func nonCanonical(s string) string {
	const alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/"
	i := strings.IndexByte(alphabet, s[len(s)-1])
	return s[:len(s)-1] + string(alphabet[i|1])
}

func ageFiles() {
	x := must(age.ParseX25519Identity(string(read("alice.identity")))).Recipient()
	other := must(age.ParseX25519Identity(string(read("bob.identity")))).Recipient()
	pq := must(age.ParseHybridIdentity(string(read("hybrid.identity")))).Recipient()
	write("alice.recipient", []byte(x.String()))
	write("bad-padding.identity", badPadding(read("alice.identity")))
	sc := must(age.NewScryptRecipient("password"))
	sc.SetWorkFactor(10)
	plain := []byte("test payload")
	for _, c := range []struct {
		name      string
		recipient age.Recipient
	}{
		{"x25519", x}, {"hybrid", pq}, {"scrypt", sc},
		{"hybrid_and_x25519", pq}, {"x25519_grease", x}, {"hybrid_grease", pq},
		{"scrypt_and_x25519", sc}, {"x25519_multiple_recipients", x},
		{"stanza_empty_body", x}, {"stanza_empty_last_line", x}, {"stanza_valid_characters", x},
		{"x25519_not_canonical_share", x}, {"hybrid_not_canonical_enc", pq},
		{"scrypt_work_factor_23", sc}, {"scrypt_not_canonical_salt", sc},
		{"x25519_low_order", x}, {"hybrid_low_order", pq},
	} {
		encrypt(c.name, stanzaList(func(key []byte) []*age.Stanza {
			s := must(c.recipient.Wrap(key))
			switch c.name {
			case "hybrid_and_x25519", "scrypt_and_x25519", "x25519_multiple_recipients":
				return append(must(other.Wrap(key)), s...)
			case "x25519_grease", "hybrid_grease":
				unknown := &age.Stanza{Type: "grease", Args: []string{"ignored"}, Body: []byte{1}}
				return append([]*age.Stanza{unknown}, append(s, unknown)...)
			case "stanza_empty_body":
				return append(s, &age.Stanza{Type: "empty"})
			case "stanza_empty_last_line":
				return append(s, &age.Stanza{Type: "stanza", Body: make([]byte, 48)})
			case "stanza_valid_characters":
				return append([]*age.Stanza{{Type: "!\"#$%&'", Args: []string{"[]{}~"}}}, s...)
			case "x25519_not_canonical_share", "hybrid_not_canonical_enc", "scrypt_not_canonical_salt":
				s[0].Args[0] = nonCanonical(s[0].Args[0])
			case "scrypt_work_factor_23":
				s[0].Args[1] = "23"
			case "x25519_low_order", "hybrid_low_order":
				arg := must(base64.RawStdEncoding.DecodeString(s[0].Args[0]))
				clear(arg[len(arg)-32:])
				s[0].Args[0] = base64.RawStdEncoding.EncodeToString(arg)
			}
			return s
		}), plain)
	}
	data := read("x25519.age")
	start := bytes.Index(data, []byte("\n--- ")) + 5
	end := start + bytes.IndexByte(data[start:], '\n')
	copy(data[start:end], nonCanonical(string(data[start:end])))
	write("hmac_not_canonical.age", data)
	encrypt("stream_no_final", x, make([]byte, (64<<10)+1))
	data = read("stream_no_final.age")
	write("stream_no_final.age", data[:len(data)-17])
}

// Flipping the low bit of the final data symbol changes the six-symbol Bech32
// checksum by generator[0]. Apply the same difference to retain a valid checksum.
func badPadding(identity []byte) []byte {
	const chars = "QPZRY9X8GF2TVDW0S3JN54KHCE6MUA7L"
	i := len(identity) - 7
	identity[i] = chars[strings.IndexByte(chars, identity[i])^1]
	for j := 0; j < 6; j++ {
		pos := i + 1 + j
		identity[pos] = chars[strings.IndexByte(chars, identity[pos])^((0x3b6a57b2>>(25-5*j))&31)]
	}
	return identity
}
