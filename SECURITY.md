# Security Policy

## Supported Versions

| Version | Supported          |
| ------- | ------------------ |
| 0.1.x   | :white_check_mark: |

## Security Considerations

### Current Limitations

1. **GitHub Signaling Encryption**
   - Current implementation uses simplified encryption (XOR-based) when MbedTLS is not available
   - **This is NOT secure for production use!**
   - Recommendation: Build with `WITH_DTLS=1` to use proper cryptographic libraries

2. **DES Encryption**
   - DES is deprecated and should not be used in production
   - The library uses DES for backward compatibility only
   - Recommendation: Upgrade to AES-256 for production deployments

3. **Command Injection Risk**
   - `p2p_signal_pub.c` uses `system()` calls for curl operations
   - Input validation is in place, but `libcurl` would be more secure
   - Recommendation: Replace `system()` calls with libcurl library calls

4. **Credential Storage**
   - GitHub tokens and TURN credentials are stored in plaintext in configuration
   - Recommendation: Use secure credential storage (keychain/keyring) in production

### Best Practices for Production Use

1. **Always build with proper crypto libraries:**
   ```bash
   cmake .. -DWITH_DTLS=ON -DTHREADED=ON
   make
   ```

2. **Use strong authentication keys:**
   ```c
   p2p_config_t cfg = {0};
   cfg.auth_key = "your-strong-random-key-here";  // Use a proper random key
   ```

3. **Enable DTLS for end-to-end encryption:**
   ```c
   cfg.use_dtls = true;
   cfg.dtls_server = false;  // Or true for server role
   ```

4. **Regularly update dependencies:**
   - MbedTLS (currently using 2.28.x which is EOL)
   - OpenSSL
   - usrsctp

5. **Network Security:**
   - Use firewall rules to restrict access
   - Implement rate limiting on signaling servers
   - Use VPN or secure networks when possible

## Reporting a Vulnerability

If you discover a security vulnerability, please email us at:
**security@yourdomain.com** (replace with actual contact)

Please do **NOT** open a public issue for security vulnerabilities.

### What to include:

- Description of the vulnerability
- Steps to reproduce
- Potential impact
- Suggested fix (if any)

### Response Timeline:

- **Initial Response**: Within 48 hours
- **Status Update**: Within 7 days
- **Fix Timeline**: Depends on severity
  - Critical: 7-14 days
  - High: 14-30 days
  - Medium: 30-60 days
  - Low: 60-90 days

## Security Updates

Security updates will be released as patch versions (e.g., 0.1.1) and documented in:
- [CHANGELOG.md](CHANGELOG.md)
- GitHub Security Advisories
- Project release notes

## Known Issues

### 1. GitHub Signaling Encryption (Medium Priority)

- **Issue**: Uses simplified XOR cipher when MbedTLS unavailable
- **Impact**: Signaling data can be easily decrypted
- **Mitigation**: Build with `WITH_DTLS=1`
- **Status**: Documented, workaround available

### 2. System() Command Execution (Medium Priority)

- **Issue**: Uses `system()` for curl calls
- **Impact**: Potential command injection risk
- **Mitigation**: Input validation in place, but not ideal
- **Status**: Planned for future refactoring with libcurl

### 3. MbedTLS  2.28 EOL (Low Priority)

- **Issue**: Using EOL version of MbedTLS
- **Impact**: No security patches from upstream
- **Mitigation**: Plan to upgrade to MbedTLS 3.6.x LTS
- **Status**: Scheduled for next major release

## Security Audit History

| Date | Auditor | Scope | Status |
|------|---------|-------|--------|
| 2026-02-13 | Internal | Code review | Completed |

## License

This security policy is licensed under the same terms as the project (MIT License).

---

**Last Updated**: February 13, 2026
