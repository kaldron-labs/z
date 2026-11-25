// Generate independent expected outputs for private keys created by ZtlsTestKeygen.
package main

import (
	"crypto/ecdh"
	"crypto/mlkem"
	"crypto/sha3"
	"encoding/pem"
	"fmt"
	"os"
	"os/exec"
	"path/filepath"

	"filippo.io/age/agessh"
	"filippo.io/hpke"
	"golang.org/x/crypto/ssh"
)

// Fail fixture setup as a unit. Only a completed staging directory is published.
func must[T any](v T, err error) T {
	if err != nil {
		panic(err)
	}
	return v
}

func check(err error) {
	if err != nil {
		panic(err)
	}
}

func read(name string) []byte        { return must(os.ReadFile(name)) }
func write(name string, data []byte) { check(os.WriteFile(name, data, 0600)) }

func agreement() {
	curve := ecdh.X25519()
	alice := must(curve.NewPrivateKey(read("alice")))
	bob := must(curve.NewPrivateKey(read("bob")))
	write("alice.pub", alice.PublicKey().Bytes())
	write("bob.pub", bob.PublicKey().Bytes())
	write("shared", must(alice.ECDH(bob.PublicKey())))

	hybrid := must(hpke.MLKEM768X25519().NewPrivateKey(read("hybrid")))
	pub := hybrid.PublicKey().Bytes()
	write("hybrid.pub", pub)
	enc, sender, err := hpke.NewSender(hybrid.PublicKey(), hpke.HKDFSHA256(),
		hpke.ChaCha20Poly1305(), []byte("fixture info"))
	check(err)
	write("hpke.enc", enc)
	write("hpke.ct", must(sender.Seal([]byte("fixture aad"), []byte("test payload"))))

	// X-Wing encapsulation using the independent Go ML-KEM and X25519 implementations.
	pq := must(mlkem.NewEncapsulationKey768(pub[:mlkem.EncapsulationKeySize768]))
	peer := must(curve.NewPublicKey(pub[mlkem.EncapsulationKeySize768:]))
	pqSecret, pqEnc := pq.Encapsulate()
	eph := bob.PublicKey().Bytes()
	h := sha3.New256()
	for _, part := range [][]byte{pqSecret, must(bob.ECDH(peer)), eph, peer.Bytes(), []byte(`\.//^\`)} {
		h.Write(part)
	}
	write("hybrid.enc", append(pqEnc, eph...))
	write("hybrid.shared", h.Sum(nil))
}

func sshFile(name string) {
	private := must(ssh.ParseRawPrivateKey(read(name + ".pem")))
	signer := must(ssh.NewSignerFromKey(private))
	public := ssh.MarshalAuthorizedKey(signer.PublicKey())
	write(name+".pub", public)
	write(name+".openssh", pem.EncodeToMemory(must(ssh.MarshalPrivateKey(private, ""))))
	encrypt(name, must(agessh.ParseRecipient(string(public))), []byte("test payload"))
	check(os.Remove(name + ".pem"))
}

func generate(dest, keygen string) {
	tmp := must(os.MkdirTemp(filepath.Dir(dest), ".ztls-test-keys-stage-"))
	defer os.RemoveAll(tmp)
	cwd := must(os.Getwd())
	func() {
		check(os.Chdir(tmp))
		defer func() { check(os.Chdir(cwd)) }()
		cmd := exec.Command(keygen)
		cmd.Stderr = os.Stderr
		check(cmd.Run())
		agreement()
		sshFile("rsa")
		sshFile("ed")
		ageFiles()
	}()
	if err := os.Rename(tmp, dest); err != nil {
		// A concurrent first run can publish its complete set first.
		if info, statErr := os.Stat(dest); statErr != nil || !info.IsDir() {
			panic(err)
		}
	}
}

func main() {
	if len(os.Args) != 3 {
		fmt.Fprintln(os.Stderr, "expected fixture directory and Ztls key generator")
		os.Exit(2)
	}
	defer func() {
		if err := recover(); err != nil {
			fmt.Fprintln(os.Stderr, "Ztls test fixture generation failed:", err)
			os.Exit(1)
		}
	}()
	generate(os.Args[1], os.Args[2])
}
