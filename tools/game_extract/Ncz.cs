// NCZ (nsz's compressed NCA) → the NCA it was made from (plan item 146; the layout as totk-mobile's script/extract_romfs.py reads it, Apache-2.0,
// written anew). An NCZ holds the NCA's first 0x4000 bytes as they are, then "NCZSECTN", a section count and the sections, 64 bytes each
// {offset, size, crypto type, 0, key, counter}, back to back from 0x4000 to the NCA's end, then the rest of the NCA decrypted and compressed as
// one Zstandard stream (block compression, "NCZBLOCK", is refused). Each section is encrypted again (types 3 and 4: AES-128-CTR, the IV the
// counter's first 8 bytes and the offset / 16 big-endian; type 1: none), and the NCA must hash to its file name (its content ID is the first 16
// bytes of its SHA-256), so a damaged NCZ fails here. Zstandard: ZstdSharp.Port 0.8.8 (Oleg Stepanischev, MIT). --selftest-ncz checks it on a
// synthetic NCA (tools/tests/t_game_extract_ncz.py).
using System;
using System.IO;
using System.Security.Cryptography;
using ZstdSharp;

static class Ncz {
    public sealed class Bad : Exception { public Bad(string m) : base(m) { } }
    const int Head = 0x4000, Chunk = 4 << 20;
    record Section(long Offset, long Size, long Crypto, byte[] Key, byte[] Counter);

    // src: the NCZ from its start (seekable); name: its file name (<content id>.ncz); dst: the NCA written. Returns the NCA's size
    public static long Restore(Stream src, string name, Stream dst) {
        string id = Path.GetFileNameWithoutExtension(name).ToLowerInvariant();
        if (id.Length != 32 || !IsHex(id)) throw new Bad($"{name}: an NCZ is named after its content ID (32 hex digits)");
        byte[] head = Exact(src, Head);
        byte[] t = Exact(src, 16);
        if (!t.AsSpan(0, 8).SequenceEqual("NCZSECTN"u8)) throw new Bad($"{name}: no NCZSECTN section table at 0x4000");
        long count = BitConverter.ToInt64(t, 8);
        if (count < 1 || count > 65536) throw new Bad($"{name}: {count} sections");
        var sections = new Section[count]; long end = Head;
        for (long i = 0; i < count; i++) {
            byte[] e = Exact(src, 64);
            var s = new Section(BitConverter.ToInt64(e, 0), BitConverter.ToInt64(e, 8), BitConverter.ToInt64(e, 16), e[32..48], e[48..64]);
            if (s.Offset != end || s.Offset % 16 != 0 || s.Size <= 0 || BitConverter.ToInt64(e, 24) != 0 || s.Crypto is not (1 or 3 or 4))
                throw new Bad($"{name}: section {i} ({s.Offset:x} +{s.Size:x}, crypto type {s.Crypto}) does not follow the one before it as NCZ sections do");
            sections[i] = s; end = s.Offset + s.Size;
        }
        byte[] magic = Exact(src, 4);
        if (magic.AsSpan().SequenceEqual("NCZB"u8)) throw new Bad($"{name}: block-compressed (NCZBLOCK); only solid Zstandard NCZ is read");
        if (!magic.AsSpan().SequenceEqual(new byte[] { 0x28, 0xb5, 0x2f, 0xfd })) throw new Bad($"{name}: no Zstandard stream after the section table");
        src.Seek(-4, SeekOrigin.Current);
        using var sha = SHA256.Create();
        dst.Write(head); sha.TransformBlock(head, 0, head.Length, null, 0);
        using var z = new DecompressionStream(src, leaveOpen: true);
        byte[] buf = new byte[Chunk]; long reported = 0;
        foreach (Section s in sections) {
            Ctr ctr = s.Crypto is 3 or 4 ? new Ctr(s.Key, s.Counter, s.Offset) : null;
            for (long done = 0; done < s.Size;) {
                int n = (int)Math.Min(Chunk, s.Size - done);
                ReadAll(z, buf, n, name);
                ctr?.Apply(buf.AsSpan(0, n));
                dst.Write(buf, 0, n); sha.TransformBlock(buf, 0, n, null, 0);
                done += n;
                if ((s.Offset + done) >> 30 > reported) { reported = (s.Offset + done) >> 30; Console.WriteLine($"  {name}: {reported} GiB restored"); }
            }
        }
        if (z.Read(buf, 0, 1) != 0) throw new Bad($"{name}: the Zstandard stream holds more than its sections");
        sha.TransformFinalBlock(Array.Empty<byte>(), 0, 0);
        string got = Convert.ToHexString(sha.Hash, 0, 16).ToLowerInvariant();
        if (got != id && Environment.GetEnvironmentVariable("DM_TEST_NCZ_NO_HASH") != "1")   // DM_TEST_NCZ_NO_HASH: negative control of t_game_extract_ncz only
            throw new Bad($"{name}: the restored NCA hashes to {got}, not its name: a damaged NCZ");
        return end;
    }

    // AES-128-CTR from a section's offset: the IV is the counter's first 8 bytes and offset / 16 big-endian, counted up per 16-byte block
    sealed class Ctr {
        readonly Aes aes = Aes.Create(); readonly byte[] iv = new byte[16]; byte[] blocks = Array.Empty<byte>(), stream = Array.Empty<byte>();
        ulong block;
        public Ctr(byte[] key, byte[] counter, long offset) { aes.Key = key; Array.Copy(counter, iv, 8); block = (ulong)offset / 16; }
        public void Apply(Span<byte> d) {   // d starts on a block boundary (sections and chunks are 16-byte multiples but a section's last chunk)
            int nb = (d.Length + 15) / 16;
            if (blocks.Length < nb * 16) { blocks = new byte[nb * 16]; stream = new byte[nb * 16]; }
            for (int i = 0; i < nb; i++) { Array.Copy(iv, 0, blocks, i * 16, 8); ulong b = block + (ulong)i; for (int k = 0; k < 8; k++) blocks[i * 16 + 8 + k] = (byte)(b >> (56 - 8 * k)); }
            aes.EncryptEcb(blocks.AsSpan(0, nb * 16), stream.AsSpan(0, nb * 16), PaddingMode.None);
            for (int i = 0; i < d.Length; i++) d[i] ^= stream[i];
            block += (ulong)nb;
        }
    }

    static byte[] Exact(Stream s, int n) { byte[] b = new byte[n]; ReadAll(s, b, n, "NCZ"); return b; }
    static void ReadAll(Stream s, byte[] b, int n, string what) {
        for (int got = 0; got < n;) { int r = s.Read(b, got, n - got); if (r <= 0) throw new Bad($"{what}: ends early"); got += r; }
    }
    static bool IsHex(string s) { foreach (char c in s) if (!Uri.IsHexDigit(c)) return false; return true; }

    // A synthetic NCA of three sections (none, AES-CTR, none) made into an NCZ and restored: the same bytes; one named after other bytes, a
    // block-compressed one and one with a gap between sections refused. Prints a line per check; returns the failures
    public static int SelfTest() {
        var rnd = new Random(146); int fails = 0;
        void Check(string what, bool ok) { Console.WriteLine($"{what,-84} {(ok ? "✓" : "✗")}"); if (!ok) fails++; }
        byte[] key = new byte[16], counter = new byte[16]; rnd.NextBytes(key); rnd.NextBytes(counter);
        long[][] secs = { new long[] { 0x4000, 0x1000, 1 }, new long[] { 0x5000, 0x3000, 3 }, new long[] { 0x8000, 0x2008, 1 } };
        int size = 0xa008; byte[] plain = new byte[size]; rnd.NextBytes(plain);
        byte[] nca = (byte[])plain.Clone();
        new Ctr(key, counter, 0x5000).Apply(nca.AsSpan(0x5000, 0x3000));   // the NCA holds that section encrypted
        string id = Convert.ToHexString(SHA256.HashData(nca), 0, 16).ToLowerInvariant();
        byte[] Make(long[][] ss, bool block) {
            var m = new MemoryStream(); m.Write(nca, 0, Head); m.Write("NCZSECTN"u8); m.Write(BitConverter.GetBytes((long)ss.Length));
            foreach (long[] s in ss) { m.Write(BitConverter.GetBytes(s[0])); m.Write(BitConverter.GetBytes(s[1])); m.Write(BitConverter.GetBytes(s[2])); m.Write(BitConverter.GetBytes(0L));
                                       m.Write(s[2] == 3 ? key : new byte[16]); m.Write(s[2] == 3 ? counter : new byte[16]); }
            if (block) m.Write("NCZBLOCK"u8);
            else { using var c = new Compressor(3); m.Write(c.Wrap(plain.AsSpan(Head))); }
            return m.ToArray();
        }
        string Try(byte[] ncz, string name, out byte[] got) {
            var o = new MemoryStream(); got = null;
            try { Restore(new MemoryStream(ncz), name, o); got = o.ToArray(); return null; } catch (Bad e) { return e.Message; }
        }
        string err = Try(Make(secs, false), id + ".ncz", out byte[] back);
        Check($"three sections (none, AES-CTR, none) restored to the same {size} bytes", err == null && back.AsSpan().SequenceEqual(nca));
        string other = (id[0] == '0' ? "1" : "0") + id[1..];
        err = Try(Make(secs, false), other + ".ncz", out _);
        Check("named after other bytes: refused (the hash check)", err != null && err.Contains("hashes to"));
        err = Try(Make(secs, true), id + ".ncz", out _);
        Check("block-compressed: refused", err != null && err.Contains("NCZBLOCK"));
        err = Try(Make(new[] { secs[0], new long[] { 0x5010, 0x2ff0, 3 }, secs[2] }, false), id + ".ncz", out _);
        Check("a gap between sections: refused", err != null && err.Contains("does not follow"));
        Console.WriteLine($"== 4 checks, {fails} failed ==");
        return fails;
    }
}
