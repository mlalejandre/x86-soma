/* SSDT de prueba del 168: un panel con _BCL/_BCM (aviso de video), el HKEY de un ThinkPad (LEN0268) y una tapa.
 * Al activarse el HKEY (MHKC 1) el firmware de prueba simula Fn+brillo arriba: avisa al panel (0x86) y al HKEY, que
 * entrega 0x1010 (la misma pulsacion: SOMA debe contar un solo paso) y 0x101B (silencio del microfono); y la tapa avisa. */
DefinitionBlock ("hk.aml", "SSDT", 2, "SOMA", "HKTEST", 1)
{
    Scope (\_SB)
    {
        Device (VID)
        {
            Name (_HID, "SOMA0001")
            Name (DOSV, 0xFF)
            Method (_DOS, 1) { DOSV = Arg0 }
            Device (LCD0)
            {
                Name (_ADR, 0x0400)
                Method (_BCL, 0) { Return (Package () { 100, 50, 10, 20, 30, 40, 50, 60, 70, 80, 90, 100 }) }
                Method (_BCM, 1) { }
            }
        }
        Device (LID)
        {
            Name (_HID, EisaId ("PNP0C0D"))
            Method (_LID, 0) { Return (1) }
            Method (_STA, 0) { Return (0x0F) }
        }
        Device (HKEY)
        {
            Name (_HID, "LEN0268")
            Name (QN, 0)
            Name (MCNT, 0)
            Method (_STA, 0) { Return (0x0F) }
            Method (MHKV, 0) { Return (0x0200) }
            Method (MHKA, 1) { Return (0x04018000) }
            Method (MHKM, 2) { MCNT++ }
            Method (MHKC, 1)
            {
                If (Arg0 == 1)
                {
                    QN = 1
                    Notify (\_SB.VID.LCD0, 0x86)
                    Notify (\_SB.HKEY, 0x80)
                    Notify (\_SB.LID, 0x80)
                }
            }
            Method (MHKP, 0)
            {
                If (QN == 1) { QN = 2; Return (0x1010) }
                If (QN == 2) { QN = 3; Return (0x101B) }
                Return (0)
            }
        }
    }
}
