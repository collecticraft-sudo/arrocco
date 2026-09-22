/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 *  Copyright (C) 2016-2021, Rasmus Althoff <info@ct800.net>
 *
 *  This file is part of the CT800 (utility functions).
 *
 *  CT800/NGPlay is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  CT800/NGPlay is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with CT800/NGPlay. If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include <stdint.h>
#include <stddef.h>
#include "ctdefs.h"

/*--------- no external variables ---------*/

static uint32_t randomness_state;

/*a CRC is needed when loading/saving a game for checking that the contents of the
backup RAM actually contain a saved game and not just arbitrary data.

used polynomial: 0xEDB88320

CRC algorithm with 8 bits per iteration: invented by Dilip V. Sarwate in 1988.*/

static const uint32_t Crc32Table[256] = {
    0x00000000U, 0x77073096U, 0xEE0E612CU, 0x990951BAU, 0x076DC419U, 0x706AF48FU, 0xE963A535U, 0x9E6495A3U,
    0x0EDB8832U, 0x79DCB8A4U, 0xE0D5E91EU, 0x97D2D988U, 0x09B64C2BU, 0x7EB17CBDU, 0xE7B82D07U, 0x90BF1D91U,
    0x1DB71064U, 0x6AB020F2U, 0xF3B97148U, 0x84BE41DEU, 0x1ADAD47DU, 0x6DDDE4EBU, 0xF4D4B551U, 0x83D385C7U,
    0x136C9856U, 0x646BA8C0U, 0xFD62F97AU, 0x8A65C9ECU, 0x14015C4FU, 0x63066CD9U, 0xFA0F3D63U, 0x8D080DF5U,
    0x3B6E20C8U, 0x4C69105EU, 0xD56041E4U, 0xA2677172U, 0x3C03E4D1U, 0x4B04D447U, 0xD20D85FDU, 0xA50AB56BU,
    0x35B5A8FAU, 0x42B2986CU, 0xDBBBC9D6U, 0xACBCF940U, 0x32D86CE3U, 0x45DF5C75U, 0xDCD60DCFU, 0xABD13D59U,
    0x26D930ACU, 0x51DE003AU, 0xC8D75180U, 0xBFD06116U, 0x21B4F4B5U, 0x56B3C423U, 0xCFBA9599U, 0xB8BDA50FU,
    0x2802B89EU, 0x5F058808U, 0xC60CD9B2U, 0xB10BE924U, 0x2F6F7C87U, 0x58684C11U, 0xC1611DABU, 0xB6662D3DU,
    0x76DC4190U, 0x01DB7106U, 0x98D220BCU, 0xEFD5102AU, 0x71B18589U, 0x06B6B51FU, 0x9FBFE4A5U, 0xE8B8D433U,
    0x7807C9A2U, 0x0F00F934U, 0x9609A88EU, 0xE10E9818U, 0x7F6A0DBBU, 0x086D3D2DU, 0x91646C97U, 0xE6635C01U,
    0x6B6B51F4U, 0x1C6C6162U, 0x856530D8U, 0xF262004EU, 0x6C0695EDU, 0x1B01A57BU, 0x8208F4C1U, 0xF50FC457U,
    0x65B0D9C6U, 0x12B7E950U, 0x8BBEB8EAU, 0xFCB9887CU, 0x62DD1DDFU, 0x15DA2D49U, 0x8CD37CF3U, 0xFBD44C65U,
    0x4DB26158U, 0x3AB551CEU, 0xA3BC0074U, 0xD4BB30E2U, 0x4ADFA541U, 0x3DD895D7U, 0xA4D1C46DU, 0xD3D6F4FBU,
    0x4369E96AU, 0x346ED9FCU, 0xAD678846U, 0xDA60B8D0U, 0x44042D73U, 0x33031DE5U, 0xAA0A4C5FU, 0xDD0D7CC9U,
    0x5005713CU, 0x270241AAU, 0xBE0B1010U, 0xC90C2086U, 0x5768B525U, 0x206F85B3U, 0xB966D409U, 0xCE61E49FU,
    0x5EDEF90EU, 0x29D9C998U, 0xB0D09822U, 0xC7D7A8B4U, 0x59B33D17U, 0x2EB40D81U, 0xB7BD5C3BU, 0xC0BA6CADU,
    0xEDB88320U, 0x9ABFB3B6U, 0x03B6E20CU, 0x74B1D29AU, 0xEAD54739U, 0x9DD277AFU, 0x04DB2615U, 0x73DC1683U,
    0xE3630B12U, 0x94643B84U, 0x0D6D6A3EU, 0x7A6A5AA8U, 0xE40ECF0BU, 0x9309FF9DU, 0x0A00AE27U, 0x7D079EB1U,
    0xF00F9344U, 0x8708A3D2U, 0x1E01F268U, 0x6906C2FEU, 0xF762575DU, 0x806567CBU, 0x196C3671U, 0x6E6B06E7U,
    0xFED41B76U, 0x89D32BE0U, 0x10DA7A5AU, 0x67DD4ACCU, 0xF9B9DF6FU, 0x8EBEEFF9U, 0x17B7BE43U, 0x60B08ED5U,
    0xD6D6A3E8U, 0xA1D1937EU, 0x38D8C2C4U, 0x4FDFF252U, 0xD1BB67F1U, 0xA6BC5767U, 0x3FB506DDU, 0x48B2364BU,
    0xD80D2BDAU, 0xAF0A1B4CU, 0x36034AF6U, 0x41047A60U, 0xDF60EFC3U, 0xA867DF55U, 0x316E8EEFU, 0x4669BE79U,
    0xCB61B38CU, 0xBC66831AU, 0x256FD2A0U, 0x5268E236U, 0xCC0C7795U, 0xBB0B4703U, 0x220216B9U, 0x5505262FU,
    0xC5BA3BBEU, 0xB2BD0B28U, 0x2BB45A92U, 0x5CB36A04U, 0xC2D7FFA7U, 0xB5D0CF31U, 0x2CD99E8BU, 0x5BDEAE1DU,
    0x9B64C2B0U, 0xEC63F226U, 0x756AA39CU, 0x026D930AU, 0x9C0906A9U, 0xEB0E363FU, 0x72076785U, 0x05005713U,
    0x95BF4A82U, 0xE2B87A14U, 0x7BB12BAEU, 0x0CB61B38U, 0x92D28E9BU, 0xE5D5BE0DU, 0x7CDCEFB7U, 0x0BDBDF21U,
    0x86D3D2D4U, 0xF1D4E242U, 0x68DDB3F8U, 0x1FDA836EU, 0x81BE16CDU, 0xF6B9265BU, 0x6FB077E1U, 0x18B74777U,
    0x88085AE6U, 0xFF0F6A70U, 0x66063BCAU, 0x11010B5CU, 0x8F659EFFU, 0xF862AE69U, 0x616BFFD3U, 0x166CCF45U,
    0xA00AE278U, 0xD70DD2EEU, 0x4E048354U, 0x3903B3C2U, 0xA7672661U, 0xD06016F7U, 0x4969474DU, 0x3E6E77DBU,
    0xAED16A4AU, 0xD9D65ADCU, 0x40DF0B66U, 0x37D83BF0U, 0xA9BCAE53U, 0xDEBB9EC5U, 0x47B2CF7FU, 0x30B5FFE9U,
    0xBDBDF21CU, 0xCABAC28AU, 0x53B39330U, 0x24B4A3A6U, 0xBAD03605U, 0xCDD70693U, 0x54DE5729U, 0x23D967BFU,
    0xB3667A2EU, 0xC4614AB8U, 0x5D681B02U, 0x2A6F2B94U, 0xB40BBE37U, 0xC30C8EA1U, 0x5A05DF1BU, 0x2D02EF8DU
};

static const uint8_t crc8_table[256] = {
    0x00U, 0x3EU, 0x7CU, 0x42U, 0xF8U, 0xC6U, 0x84U, 0xBAU,
    0x95U, 0xABU, 0xE9U, 0xD7U, 0x6DU, 0x53U, 0x11U, 0x2FU,
    0x4FU, 0x71U, 0x33U, 0x0DU, 0xB7U, 0x89U, 0xCBU, 0xF5U,
    0xDAU, 0xE4U, 0xA6U, 0x98U, 0x22U, 0x1CU, 0x5EU, 0x60U,
    0x9EU, 0xA0U, 0xE2U, 0xDCU, 0x66U, 0x58U, 0x1AU, 0x24U,
    0x0BU, 0x35U, 0x77U, 0x49U, 0xF3U, 0xCDU, 0x8FU, 0xB1U,
    0xD1U, 0xEFU, 0xADU, 0x93U, 0x29U, 0x17U, 0x55U, 0x6BU,
    0x44U, 0x7AU, 0x38U, 0x06U, 0xBCU, 0x82U, 0xC0U, 0xFEU,
    0x59U, 0x67U, 0x25U, 0x1BU, 0xA1U, 0x9FU, 0xDDU, 0xE3U,
    0xCCU, 0xF2U, 0xB0U, 0x8EU, 0x34U, 0x0AU, 0x48U, 0x76U,
    0x16U, 0x28U, 0x6AU, 0x54U, 0xEEU, 0xD0U, 0x92U, 0xACU,
    0x83U, 0xBDU, 0xFFU, 0xC1U, 0x7BU, 0x45U, 0x07U, 0x39U,
    0xC7U, 0xF9U, 0xBBU, 0x85U, 0x3FU, 0x01U, 0x43U, 0x7DU,
    0x52U, 0x6CU, 0x2EU, 0x10U, 0xAAU, 0x94U, 0xD6U, 0xE8U,
    0x88U, 0xB6U, 0xF4U, 0xCAU, 0x70U, 0x4EU, 0x0CU, 0x32U,
    0x1DU, 0x23U, 0x61U, 0x5FU, 0xE5U, 0xDBU, 0x99U, 0xA7U,
    0xB2U, 0x8CU, 0xCEU, 0xF0U, 0x4AU, 0x74U, 0x36U, 0x08U,
    0x27U, 0x19U, 0x5BU, 0x65U, 0xDFU, 0xE1U, 0xA3U, 0x9DU,
    0xFDU, 0xC3U, 0x81U, 0xBFU, 0x05U, 0x3BU, 0x79U, 0x47U,
    0x68U, 0x56U, 0x14U, 0x2AU, 0x90U, 0xAEU, 0xECU, 0xD2U,
    0x2CU, 0x12U, 0x50U, 0x6EU, 0xD4U, 0xEAU, 0xA8U, 0x96U,
    0xB9U, 0x87U, 0xC5U, 0xFBU, 0x41U, 0x7FU, 0x3DU, 0x03U,
    0x63U, 0x5DU, 0x1FU, 0x21U, 0x9BU, 0xA5U, 0xE7U, 0xD9U,
    0xF6U, 0xC8U, 0x8AU, 0xB4U, 0x0EU, 0x30U, 0x72U, 0x4CU,
    0xEBU, 0xD5U, 0x97U, 0xA9U, 0x13U, 0x2DU, 0x6FU, 0x51U,
    0x7EU, 0x40U, 0x02U, 0x3CU, 0x86U, 0xB8U, 0xFAU, 0xC4U,
    0xA4U, 0x9AU, 0xD8U, 0xE6U, 0x5CU, 0x62U, 0x20U, 0x1EU,
    0x31U, 0x0FU, 0x4DU, 0x73U, 0xC9U, 0xF7U, 0xB5U, 0x8BU,
    0x75U, 0x4BU, 0x09U, 0x37U, 0x8DU, 0xB3U, 0xF1U, 0xCFU,
    0xE0U, 0xDEU, 0x9CU, 0xA2U, 0x18U, 0x26U, 0x64U, 0x5AU,
    0x3AU, 0x04U, 0x46U, 0x78U, 0xC2U, 0xFCU, 0xBEU, 0x80U,
    0xAFU, 0x91U, 0xD3U, 0xEDU, 0x57U, 0x69U, 0x2BU, 0x15U
};
/*CRC table generator, just for info:
#define POLYNOMIAL  0xEDB88320U
uint32_t crc, i, j;
for (i = 0; i <= 0xFFU; i++)
{
    crc = i;
    for (j = 0; j < 8; j++)
        crc = (crc >> 1) ^ (-(int32_t)(crc & 1) & POLYNOMIAL);
    Crc32Table[i] = crc;
}

CRC-8 similarly with polynomial 0xB2. For the reason choosing this polynomial,
cf. the paper "Cyclic Redundancy Code (CRC) Polynomial Selection For
Embedded Networks" by Philip Koopman and Tridib Chakravarty. The polynomial 0xA6
(in Koopman notation) is optimum for data length > 120 bits (a book position
has 65 bytes, i.e. 520 bits). 0xA6 in Koopman notation is x^8 +x^6 +x^3 +x^2 +1.
In conventional notation, this 0x4D (the x^8 term is left out). What we need is
the reverse of that, and that is 0xB2.
*/

/*CRC algorithm with 8 bits per iteration: invented by Dilip V. Sarwate in 1988.*/
uint32_t Util_Crc32(const void *buffer, size_t len)
{
    uint32_t crc = 0xFFFFFFFFU;
    const uint8_t *databyte = (const uint8_t *) buffer;
    size_t rest_len = len & 7U;

    /*unroll the CRC loop 8 times.*/
    len >>= 3;
    while (len--)
    {
        crc = (crc >> 8) ^ Crc32Table[(uint8_t)(crc ^ *databyte++)];
        crc = (crc >> 8) ^ Crc32Table[(uint8_t)(crc ^ *databyte++)];
        crc = (crc >> 8) ^ Crc32Table[(uint8_t)(crc ^ *databyte++)];
        crc = (crc >> 8) ^ Crc32Table[(uint8_t)(crc ^ *databyte++)];
        crc = (crc >> 8) ^ Crc32Table[(uint8_t)(crc ^ *databyte++)];
        crc = (crc >> 8) ^ Crc32Table[(uint8_t)(crc ^ *databyte++)];
        crc = (crc >> 8) ^ Crc32Table[(uint8_t)(crc ^ *databyte++)];
        crc = (crc >> 8) ^ Crc32Table[(uint8_t)(crc ^ *databyte++)];
    }

    /*do the possible leftovers from the unrolled loop.*/
    while (rest_len--)
        crc = (crc >> 8) ^ Crc32Table[(uint8_t)(crc ^ *databyte++)];

    return(~crc);
}

uint8_t Util_Crc8(const void *buffer, size_t len)
{
    uint8_t crc = 0xFFU;
    const uint8_t *databyte = (const uint8_t *) buffer;
    size_t rest_len = len & 7U;

    /*unroll the CRC loop 8 times.*/
    len >>= 3;
    while (len--)
    {
        crc = crc8_table[crc ^ *databyte++];
        crc = crc8_table[crc ^ *databyte++];
        crc = crc8_table[crc ^ *databyte++];
        crc = crc8_table[crc ^ *databyte++];
        crc = crc8_table[crc ^ *databyte++];
        crc = crc8_table[crc ^ *databyte++];
        crc = crc8_table[crc ^ *databyte++];
        crc = crc8_table[crc ^ *databyte++];
    }

    /*do the possible leftovers from the unrolled loop.*/
    while (rest_len--)
        crc = crc8_table[crc ^ *databyte++];

    return(~crc);
}

void Util_Seed(uint32_t seed)
{
    randomness_state = seed;
}

/*returns an integer between (including) 0 and range-1.*/
uint32_t Util_Rand(uint32_t range)
{
    uint32_t new_state = (randomness_state * 1103515245U) + 12345U;

    randomness_state = new_state;
    return(((new_state >> 16) * range) >> 16);
}

/*that's used for retrieving the CRC from the opening book. To avoid
possible endianess issues in the future, the opening book tool has
saved the 32bit CRC bytewise, most significant byte first.*/
uint32_t Util_Hex_Long_To_Int(const uint8_t *buffer)
{
    uint32_t ret;

    ret = *buffer++;
    ret = (ret << 8) | *buffer++;
    ret = (ret << 8) | *buffer++;
    ret = (ret << 8) | *buffer;

    return(ret);
}

/*some integer conversion routines for avoiding the parsing of the format
  strings in sprintf. these routines are about 2-3 times as fast.*/

/*uint16_t to string*/
int Util_Tostring_U16(char *buf, uint16_t val)
{
    int len, i;
    static const uint16_t divisors[5] = {1U, 10U, 100U, 1000U, 10000U};

    for (i = 4; i != 0 && divisors[i] > val; i--) ;

    len = i + 1;

    while (i >= 1)
    {
        uint16_t divisor;
        int digit;

        divisor = divisors[i];
        digit = val / divisor;
        val %= divisor;
        *buf++ = digit + '0';
        i--;
    }

    *buf++ = val + '0';
    *buf = 0;

    return(len);
}

/*int16_t to string*/
int Util_Tostring_I16(char *buf, int16_t val)
{
    int len;

    if (val < 0)
    {
        /*doesn't work for -INT_MAX, but that case doesn't happen.
          a dedicated check plus plain strcpy() would do in case of need.*/
        val = -val;
        *buf++ = '-';
        len = 1;
    }
    else
        len = 0;

    len += Util_Tostring_U16(buf, (uint16_t) val);

    return(len);
}

/*uint32_t to string*/
int Util_Tostring_U32(char *buf, uint32_t val)
{
    int len, i;
    static const uint32_t divisors[10] = {1U,
                                          10U, 100U, 1000U,
                                          10000U, 100000U, 1000000U,
                                          10000000U, 100000000U, 1000000000U};
    for (i = 9; i != 0 && divisors[i] > val; i--) ;

    len = i + 1;

    while (i >= 1)
    {
        uint32_t divisor;
        int digit;

        divisor = divisors[i];
        digit = val / divisor;
        val %= divisor;
        *buf++ = digit + '0';
        i--;
    }

    *buf++ = val + '0';
    *buf = 0;

    return(len);
}

/*int32_t to string*/
int Util_Tostring_I32(char *buf, int32_t val)
{
    int len;

    if (val < 0)
    {
        /*doesn't work for -INT_MAX, but that case doesn't happen.
          a dedicated check plus plain strcpy() would do in case of need.*/
        val = -val;
        *buf++ = '-';
        len = 1;
    }
    else
        len = 0;

    len += Util_Tostring_U32(buf, (uint32_t) val);

    return(len);
}

/*uint64_t to string*/
int Util_Tostring_U64(char *buf, uint64_t val)
{
    int len, i;
    uint32_t val32;
    static const uint32_t divisors32[9] =  {1U,
                                            10U, 100U, 1000U,
                                            10000U, 100000U, 1000000U,
                                            10000000U, 100000000U};
    static const uint64_t divisors64[20] = {1U,
                                            10U, 100U, 1000U,
                                            10000U, 100000U, 1000000U,
                                            10000000U, 100000000U, 1000000000U,
                                            10000000000U, 100000000000U, 1000000000000U,
                                            10000000000000U, 100000000000000U, 1000000000000000U,
                                            10000000000000000U, 100000000000000000U, 1000000000000000000U,
                                            10000000000000000000U};
    if (val <= 0xFFFFFFFFU) /*32 bit value range*/
        return(Util_Tostring_U32(buf, (uint32_t) val));

    for (i = 19; i != 0 && divisors64[i] > val; i--) ;

    len = i + 1;

    /*numbers between 2^32, i.e. 4.3e9, and 10e9 are not yet 32 bit,
      therefore index 9, which is 1e9.*/
    while (i >= 9)
    {
        uint64_t divisor;
        int digit;

        divisor = divisors64[i];
        digit = val / divisor;
        val %= divisor;
        *buf++ = digit + '0';
        i--;
    }

    val32 = (uint32_t) val;

    while (i >= 1)
    {
        uint32_t divisor;
        int digit;

        divisor = divisors32[i];
        digit = val32 / divisor;
        val32 %= divisor;
        *buf++ = digit + '0';
        i--;
    }

    *buf++ = val32 + '0';
    *buf = 0;

    return(len);
}

/*int64_t to string*/
int Util_Tostring_I64(char *buf, int64_t val)
{
    int len;

    if (val < 0)
    {
        /*doesn't work for -INT_MAX, but that case doesn't happen.
          a dedicated check plus plain strcpy() would do in case of need.*/
        val = -val;
        *buf++ = '-';
        len = 1;
    }
    else
        len = 0;

    len += Util_Tostring_U64(buf, (uint64_t) val);

    return(len);
}
