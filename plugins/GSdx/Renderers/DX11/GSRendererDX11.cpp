/*
 *	Copyright (C) 2007-2009 Gabest
 *	http://www.gabest.org
 *
 *  This Program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
 *
 *  This Program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with GNU Make; see the file COPYING.  If not, write to
 *  the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA USA.
 *  http://www.gnu.org/copyleft/gpl.html
 *
 */

#include "stdafx.h"
#include "GSRendererDX11.h"

GSRendererDX11::GSRendererDX11()
	: GSRendererHW(new GSTextureCache11(this))
{
	if (theApp.GetConfigB("UserHacks"))
	{
		UserHacks_AlphaHack    = theApp.GetConfigB("UserHacks_AlphaHack");
		UserHacks_AlphaStencil = theApp.GetConfigB("UserHacks_AlphaStencil");
	}
	else
	{
		UserHacks_AlphaHack    = false;
		UserHacks_AlphaStencil = false;
	}

	ResetStates();
}

bool GSRendererDX11::CreateDevice(GSDevice* dev)
{
	return GSRenderer::CreateDevice(dev);
}

void GSRendererDX11::SetupIA(const float& sx, const float& sy)
{
	GSDevice11* dev = (GSDevice11*)m_dev;

	D3D11_PRIMITIVE_TOPOLOGY t;

	bool unscale_pt_ln = m_userHacks_enabled_unscale_ptln && (GetUpscaleMultiplier() != 1);

	switch (m_vt.m_primclass)
	{
	case GS_POINT_CLASS:
		if (unscale_pt_ln)
		{
			m_gs_sel.point = 1;
			gs_cb.PointSize = GSVector2(16.0f * sx, 16.0f * sy);
		}

		t = D3D11_PRIMITIVE_TOPOLOGY_POINTLIST;
		break;
	case GS_LINE_CLASS:
		if (unscale_pt_ln)
		{
			m_gs_sel.line = 1;
			gs_cb.PointSize = GSVector2(16.0f * sx, 16.0f * sy);
		}

		t = D3D11_PRIMITIVE_TOPOLOGY_LINELIST;

		break;
	case GS_SPRITE_CLASS:
		t = D3D11_PRIMITIVE_TOPOLOGY_LINELIST;
		break;
	case GS_TRIANGLE_CLASS:

		t = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;

		break;
	default:
		__assume(0);
	}

	void* ptr = NULL;

	if (dev->IAMapVertexBuffer(&ptr, sizeof(GSVertex), m_vertex.next))
	{
		GSVector4i::storent(ptr, m_vertex.buff, sizeof(GSVertex) * m_vertex.next);

		if (m_userhacks_wildhack && !m_isPackedUV_HackFlag)
		{
			GSVertex* RESTRICT d = (GSVertex*)ptr;

			for (unsigned int i = 0; i < m_vertex.next; i++)
			{
				if (PRIM->TME && PRIM->FST) d[i].UV &= 0x3FEF3FEF;
			}
		}

		dev->IAUnmapVertexBuffer();
	}

	dev->IASetIndexBuffer(m_index.buff, m_index.tail);
	dev->IASetPrimitiveTopology(t);
}

void GSRendererDX11::EmulateAtst(const int pass, const GSTextureCache::Source* tex)
{
	static const uint32 inverted_atst[] = {ATST_ALWAYS, ATST_NEVER, ATST_GEQUAL, ATST_GREATER, ATST_NOTEQUAL, ATST_LESS, ATST_LEQUAL, ATST_EQUAL};
	int atst = (pass == 2) ? inverted_atst[m_context->TEST.ATST] : m_context->TEST.ATST;

	if (!m_context->TEST.ATE) return;

	switch (atst)
	{
		case ATST_LESS:
			if (tex && tex->m_spritehack_t)
			{
				m_ps_sel.atst = 0;
			}
			else
			{
				ps_cb.FogColor_AREF.a = (float)m_context->TEST.AREF - 0.1f;
				m_ps_sel.atst = 1;
			}
			break;
		case ATST_LEQUAL:
			ps_cb.FogColor_AREF.a = (float)m_context->TEST.AREF - 0.1f + 1.0f;
			m_ps_sel.atst = 1;
			break;
		case ATST_GEQUAL:
			// Maybe a -1 trick multiplication factor could be used to merge with ATST_LEQUAL case
			ps_cb.FogColor_AREF.a = (float)m_context->TEST.AREF - 0.1f;
			m_ps_sel.atst = 2;
			break;
		case ATST_GREATER:
			// Maybe a -1 trick multiplication factor could be used to merge with ATST_LESS case
			ps_cb.FogColor_AREF.a = (float)m_context->TEST.AREF - 0.1f + 1.0f;
			m_ps_sel.atst = 2;
			break;
		case ATST_EQUAL:
			ps_cb.FogColor_AREF.a = (float)m_context->TEST.AREF;
			m_ps_sel.atst = 3;
			break;
		case ATST_NOTEQUAL:
			ps_cb.FogColor_AREF.a = (float)m_context->TEST.AREF;
			m_ps_sel.atst = 4;
			break;

		case ATST_NEVER: // Draw won't be done so no need to implement it in shader
		case ATST_ALWAYS:
		default:
			m_ps_sel.atst = 0;
			break;
	}
}

void GSRendererDX11::EmulateZbuffer()
{
	if (m_context->TEST.ZTE)
	{
		m_om_dssel.ztst = m_context->TEST.ZTST;
		m_om_dssel.zwe = !m_context->ZBUF.ZMSK;
	}
	else
	{
		m_om_dssel.ztst = ZTST_ALWAYS;
	}

	uint32 max_z;
	if (m_context->ZBUF.PSM == PSM_PSMZ32)
	{
		max_z = 0xFFFFFFFF;
	}
	else if (m_context->ZBUF.PSM == PSM_PSMZ24)
	{
		max_z = 0xFFFFFF;
	}
	else
	{
		max_z = 0xFFFF;
	}

	// The real GS appears to do no masking based on the Z buffer format and writing larger Z values
	// than the buffer supports seems to be an error condition on the real GS, causing it to crash.
	// We are probably receiving bad coordinates from VU1 in these cases.

	if (m_om_dssel.ztst >= ZTST_ALWAYS && m_om_dssel.zwe && (m_context->ZBUF.PSM != PSM_PSMZ32))
	{
		if (m_vt.m_max.p.z > max_z)
		{
			ASSERT(m_vt.m_min.p.z > max_z); // sfex capcom logo
			// Fixme :Following conditional fixes some dialog frame in Wild Arms 3, but may not be what was intended.
			if (m_vt.m_min.p.z > max_z)
			{
#ifdef _DEBUG
				fprintf(stdout, "Bad Z size on %s buffers\n", psm_str(m_context->ZBUF.PSM));
#endif
				m_om_dssel.ztst = ZTST_ALWAYS;
			}
		}
	}

	GSVertex* v = &m_vertex.buff[0];
	// Minor optimization of a corner case (it allow to better emulate some alpha test effects)
	if (m_om_dssel.ztst == ZTST_GEQUAL && m_vt.m_eq.z && v[0].XYZ.Z == max_z)
	{
#ifdef _DEBUG
		fprintf(stdout, "Optimize Z test GEQUAL to ALWAYS (%s)\n", psm_str(m_context->ZBUF.PSM));
#endif
		m_om_dssel.ztst = ZTST_ALWAYS;
	}
}

void GSRendererDX11::EmulateTextureShuffleAndFbmask()
{
	size_t count = m_vertex.next;
	GSVertex* v = &m_vertex.buff[0];

	if (m_texture_shuffle)
	{
		m_ps_sel.shuffle = 1;
		m_ps_sel.dfmt = 0;

		const GIFRegXYOFFSET& o = m_context->XYOFFSET;

		// vertex position is 8 to 16 pixels, therefore it is the 16-31 bits of the colors
		int  pos = (v[0].XYZ.X - o.OFX) & 0xFF;
		bool write_ba = (pos > 112 && pos < 136);
		// Read texture is 8 to 16 pixels (same as above)
		float tw = (float)(1u << m_context->TEX0.TW);
		int tex_pos = (PRIM->FST) ? v[0].U : (int)(tw * v[0].ST.S);
		tex_pos &= 0xFF;
		m_ps_sel.read_ba = (tex_pos > 112 && tex_pos < 144);

		// Convert the vertex info to a 32 bits color format equivalent
		if (PRIM->FST)
		{

			for(size_t i = 0; i < count; i += 2)
			{
				if (write_ba)
					v[i].XYZ.X   -= 128u;
				else
					v[i+1].XYZ.X += 128u;

				if (m_ps_sel.read_ba)
					v[i].U       -= 128u;
				else
					v[i+1].U     += 128u;

				// Height is too big (2x).
				int tex_offset = v[i].V & 0xF;
				GSVector4i offset(o.OFY, tex_offset, o.OFY, tex_offset);

				GSVector4i tmp(v[i].XYZ.Y, v[i].V, v[i+1].XYZ.Y, v[i+1].V);
				tmp = GSVector4i(tmp - offset).srl32(1) + offset;

				v[i].XYZ.Y   = (uint16)tmp.x;
				v[i].V       = (uint16)tmp.y;
				v[i+1].XYZ.Y = (uint16)tmp.z;
				v[i+1].V     = (uint16)tmp.w;
			}
		}
		else
		{
			const float offset_8pix = 8.0f / tw;

			for(size_t i = 0; i < count; i += 2)
			{
				if (write_ba)
					v[i].XYZ.X   -= 128u;
				else
					v[i+1].XYZ.X += 128u;

				if (m_ps_sel.read_ba)
					v[i].ST.S    -= offset_8pix;
				else
					v[i+1].ST.S  += offset_8pix;

				// Height is too big (2x).
				GSVector4i offset(o.OFY, o.OFY);

				GSVector4i tmp(v[i].XYZ.Y, v[i+1].XYZ.Y);
				tmp = GSVector4i(tmp - offset).srl32(1) + offset;

				//fprintf(stderr, "Before %d, After %d\n", v[i+1].XYZ.Y, tmp.y);
				v[i].XYZ.Y   = (uint16)tmp.x;
				v[i].ST.T   /= 2.0f;
				v[i+1].XYZ.Y = (uint16)tmp.y;
				v[i+1].ST.T /= 2.0f;
			}
		}

		// Please bang my head against the wall!
		// 1/ Reduce the frame mask to a 16 bit format
		const uint32& m = m_context->FRAME.FBMSK;
		uint32 fbmask = ((m >> 3) & 0x1F) | ((m >> 6) & 0x3E0) | ((m >> 9) & 0x7C00) | ((m >> 16) & 0x8000);
		// FIXME GSVector will be nice here
		uint8 rg_mask = fbmask & 0xFF;
		uint8 ba_mask = (fbmask >> 8) & 0xFF;
		m_om_bsel.wrgba = 0;

		// 2 Select the new mask (Please someone put SSE here)
		if (rg_mask != 0xFF)
		{
			if (write_ba)
			{
				m_om_bsel.wb = 1;
			}
			else
			{
				m_om_bsel.wr = 1;
			}
		}
		else if ((fbmask & 0xFF) != 0xFF)
		{
#ifdef _DEBUG
			fprintf(stderr, "Please fix me! wb %u wr %u\n", m_om_bsel.wb, m_om_bsel.wr);
#endif
			//ASSERT(0);
		}

		if (ba_mask != 0xFF)
		{
			if (write_ba)
			{
				m_om_bsel.wa = 1;
			}
			else
			{
				m_om_bsel.wg = 1;
			}
		}
		else if ((fbmask & 0xFF) != 0xFF)
		{
#ifdef _DEBUG
			fprintf(stderr, "Please fix me! wa %u wg %u\n", m_om_bsel.wa, m_om_bsel.wg);
#endif
			//ASSERT(0);
		}
	}
	else
	{
		m_ps_sel.dfmt = GSLocalMemory::m_psm[m_context->FRAME.PSM].fmt;

		m_om_bsel.wrgba = ~GSVector4i::load((int)m_context->FRAME.FBMSK).eq8(GSVector4i::xffffffff()).mask();
	}
}

void GSRendererDX11::EmulateChannelShuffle(GSTexture** rt, const GSTextureCache::Source* tex)
{
	GSDevice11* dev = (GSDevice11*)m_dev;

	// Uncomment to disable HLE emulation (allow to trace the draw call)
	// m_channel_shuffle = false;

	// First let's check we really have a channel shuffle effect
	if (m_channel_shuffle)
	{
		if (m_game.title == CRC::GT4 || m_game.title == CRC::GT3 || m_game.title == CRC::GTConcept || m_game.title == CRC::TouristTrophy)
		{
			// fprintf(stderr, "Gran Turismo RGB Channel\n");
			m_ps_sel.channel = ChannelFetch_RGB;
			m_context->TEX0.TFX = TFX_DECAL;
			*rt = tex->m_from_target;
		}
		else if (m_game.title == CRC::Tekken5)
		{
			if (m_context->FRAME.FBW == 1)
			{
				// Used in stages: Secret Garden, Acid Rain, Moonlit Wilderness
				// fprintf(stderr, "Tekken5 RGB Channel\n");
				m_ps_sel.channel = ChannelFetch_RGB;
				m_context->FRAME.FBMSK = 0xFF000000;
				// 12 pages: 2 calls by channel, 3 channels, 1 blit
				// Minus current draw call
				m_skip = 12 * (3 + 3 + 1) - 1;
				*rt = tex->m_from_target;
			}
			else
			{
				// Could skip model drawing if wrongly detected
				m_channel_shuffle = false;
			}
		}
		else if ((tex->m_texture->GetType() == GSTexture::DepthStencil) && !(tex->m_32_bits_fmt))
		{
			// So far 2 games hit this code path. Urban Chaos and Tales of Abyss
			// UC: will copy depth to green channel
			// ToA: will copy depth to alpha channel
			if ((m_context->FRAME.FBMSK & 0xFF0000) == 0xFF0000)
			{
				// Green channel is masked
				// fprintf(stderr, "Tales Of Abyss Crazyness (MSB 16b depth to Alpha)\n");
				m_ps_sel.tales_of_abyss_hle = 1;
			}
			else
			{
				// fprintf(stderr, "Urban Chaos Crazyness (Green extraction)\n");
				m_ps_sel.urban_chaos_hle = 1;
			}
		}
		else if (m_index.tail <= 64 && m_context->CLAMP.WMT == 3)
		{
			// Blood will tell. I think it is channel effect too but again
			// implemented in a different way. I don't want to add more CRC stuff. So
			// let's disable channel when the signature is different.
			//
			// Note: Tales Of Abyss and Tekken5 could hit this path too. Those games are
			// handled above.
			// fprintf(stderr, "Maybe not a channel!\n");
			m_channel_shuffle = false;
		}
		else if (m_context->CLAMP.WMS == 3 && ((m_context->CLAMP.MAXU & 0x8) == 8))
		{
			// Read either blue or Alpha. Let's go for Blue ;)
			// MGS3/Kill Zone
			// fprintf(stderr, "Blue channel\n");
			m_ps_sel.channel = ChannelFetch_BLUE;
		}
		else if (m_context->CLAMP.WMS == 3 && ((m_context->CLAMP.MINU & 0x8) == 0))
		{
			// Read either Red or Green. Let's check the V coordinate. 0-1 is likely top so
			// red. 2-3 is likely bottom so green (actually depends on texture base pointer offset)
			bool green = PRIM->FST && (m_vertex.buff[0].V & 32);
			if (green && (m_context->FRAME.FBMSK & 0x00FFFFFF) == 0x00FFFFFF)
			{
				// Typically used in Terminator 3
				int blue_mask = m_context->FRAME.FBMSK >> 24;
				int green_mask = ~blue_mask & 0xFF;
				int blue_shift = -1;

				// Note: potentially we could also check the value of the clut
				switch (m_context->FRAME.FBMSK >> 24)
				{
					case 0xFF: ASSERT(0);      break;
					case 0xFE: blue_shift = 1; break;
					case 0xFC: blue_shift = 2; break;
					case 0xF8: blue_shift = 3; break;
					case 0xF0: blue_shift = 4; break;
					case 0xE0: blue_shift = 5; break;
					case 0xC0: blue_shift = 6; break;
					case 0x80: blue_shift = 7; break;
					default:   ASSERT(0);      break;
				}

				int green_shift = 8 - blue_shift;
				ps_cb.ChannelShuffle = GSVector4i(blue_mask, blue_shift, green_mask, green_shift);

				if (blue_shift >= 0)
				{
					// fprintf(stderr, "Green/Blue channel (%d, %d)\n", blue_shift, green_shift);
					m_ps_sel.channel = ChannelFetch_GXBY;
					m_context->FRAME.FBMSK = 0x00FFFFFF;
				}
				else
				{
					// fprintf(stderr, "Green channel (wrong mask) (fbmask %x)\n", m_context->FRAME.FBMSK >> 24);
					m_ps_sel.channel = ChannelFetch_GREEN;
				}

			}
			else if (green)
			{
				// fprintf(stderr, "Green channel\n");
				m_ps_sel.channel = ChannelFetch_GREEN;
			}
			else
			{
				// Pop
				// fprintf(stderr, "Red channel\n");
				m_ps_sel.channel = ChannelFetch_RED;
			}
		}
		else
		{
			// fprintf(stderr, "Channel not supported\n");
			m_channel_shuffle = false;
		}
	}

	// Effect is really a channel shuffle effect so let's cheat a little
	if (m_channel_shuffle)
	{
		// FIXME: Slot 4 - unbind texture when it isn't used.
		dev->PSSetShaderResource(4, tex->m_from_target);
		// Replace current draw with a fullscreen sprite
		//
		// Performance GPU note: it could be wise to reduce the size to
		// the rendered size of the framebuffer

		GSVertex* s = &m_vertex.buff[0];
		s[0].XYZ.X = (uint16)(m_context->XYOFFSET.OFX + 0);
		s[1].XYZ.X = (uint16)(m_context->XYOFFSET.OFX + 16384);
		s[0].XYZ.Y = (uint16)(m_context->XYOFFSET.OFY + 0);
		s[1].XYZ.Y = (uint16)(m_context->XYOFFSET.OFY + 16384);

		m_vertex.head = m_vertex.tail = m_vertex.next = 2;
		m_index.tail = 2;
	}
	else
	{
#ifdef _DEBUG
		dev->PSSetShaderResource(4, NULL);
#endif
	}
}

void GSRendererDX11::EmulateTextureSampler(const GSTextureCache::Source* tex)
{
	const GSLocalMemory::psm_t &psm = GSLocalMemory::m_psm[m_context->TEX0.PSM];
	const GSLocalMemory::psm_t &cpsm = psm.pal > 0 ? GSLocalMemory::m_psm[m_context->TEX0.CPSM] : psm;

	const uint8 wms = m_context->CLAMP.WMS;
	const uint8 wmt = m_context->CLAMP.WMT;
	bool complex_wms_wmt = !!((wms | wmt) & 2);

	bool bilinear = m_vt.IsLinear();
	bool shader_emulated_sampler = tex->m_palette || cpsm.fmt != 0 || complex_wms_wmt || psm.depth;

	// 1 and 0 are equivalent
	m_ps_sel.wms = (wms & 2) ? wms : 0;
	m_ps_sel.wmt = (wmt & 2) ? wmt : 0;

	int w = tex->m_texture->GetWidth();
	int h = tex->m_texture->GetHeight();

	int tw = (int)(1 << m_context->TEX0.TW);
	int th = (int)(1 << m_context->TEX0.TH);

	GSVector4 WH(tw, th, w, h);

	// Depth + bilinear filtering isn't done yet (And I'm not sure we need it anyway but a game will prove me wrong)
	// So of course, GTA set the linear mode, but sampling is done at texel center so it is equivalent to nearest sampling
	ASSERT(!(psm.depth && m_vt.IsLinear()));

	// Performance note:
	// 1/ Don't set 0 as it is the default value
	// 2/ Only keep aem when it is useful (avoid useless shader permutation)
	if (m_ps_sel.shuffle)
	{
		// Force a 32 bits access (normally shuffle is done on 16 bits)
		// m_ps_sel.fmt = 0; // removed as an optimization
		m_ps_sel.aem = m_env.TEXA.AEM;
		ASSERT(tex->m_target);

		// Require a float conversion if the texure is a depth otherwise uses Integral scaling
		if (psm.depth)
		{
			m_ps_sel.depth_fmt = (tex->m_texture->GetType() != GSTexture::DepthStencil) ? 3 : 1;
		}

		// Shuffle is a 16 bits format, so aem is always required
		GSVector4 ta(m_env.TEXA & GSVector4i::x000000ff());
		ps_cb.MinF_TA = (GSVector4(ps_cb.MskFix) + 0.5f).xyxy(ta) / WH.xyxy(GSVector4(255, 255));

		bilinear &= m_vt.IsLinear();

		GSVector4 half_offset = RealignTargetTextureCoordinate(tex);
		vs_cb.Texture_Scale_Offset.z = half_offset.x;
		vs_cb.Texture_Scale_Offset.w = half_offset.y;

	}
	else if (tex->m_target)
	{
		// Use an old target. AEM and index aren't resolved it must be done
		// on the GPU

		// Select the 32/24/16 bits color (AEM)
		m_ps_sel.fmt = cpsm.fmt;
		m_ps_sel.aem = m_env.TEXA.AEM;

		// Don't upload AEM if format is 32 bits
		if (cpsm.fmt)
		{
			GSVector4 ta(m_env.TEXA & GSVector4i::x000000ff());
			ps_cb.MinF_TA = (GSVector4(ps_cb.MskFix) + 0.5f).xyxy(ta) / WH.xyxy(GSVector4(255, 255));
		}

		// Select the index format
		if (tex->m_palette)
		{
			// FIXME Potentially improve fmt field in GSLocalMemory
			if (m_context->TEX0.PSM == PSM_PSMT4HL)
				m_ps_sel.fmt |= 1 << 2;
			else if (m_context->TEX0.PSM == PSM_PSMT4HH)
				m_ps_sel.fmt |= 2 << 2;
			else
				m_ps_sel.fmt |= 3 << 2;

			// Alpha channel of the RT is reinterpreted as an index. Star
			// Ocean 3 uses it to emulate a stencil buffer.  It is a very
			// bad idea to force bilinear filtering on it.
			bilinear &= m_vt.IsLinear();
		}

		// Depth format
		if (tex->m_texture->GetType() == GSTexture::DepthStencil)
		{
			// Require a float conversion if the texure is a depth format
			m_ps_sel.depth_fmt = (psm.bpp == 16) ? 2 : 1;

			// Don't force interpolation on depth format
			bilinear &= m_vt.IsLinear();
		}
		else if (psm.depth)
		{
			// Use Integral scaling
			m_ps_sel.depth_fmt = 3;

			// Don't force interpolation on depth format
			bilinear &= m_vt.IsLinear();
		}

		GSVector4 half_offset = RealignTargetTextureCoordinate(tex);
		vs_cb.Texture_Scale_Offset.z = half_offset.x;
		vs_cb.Texture_Scale_Offset.w = half_offset.y;
	}
	else if (tex->m_palette)
	{
		// Use a standard 8 bits texture. AEM is already done on the CLUT
		// Therefore you only need to set the index
		// m_ps_sel.aem     = 0; // removed as an optimization

		// Note 4 bits indexes are converted to 8 bits
		m_ps_sel.fmt = 3 << 2;

	}
	else
	{
		// Standard texture. Both index and AEM expansion were already done by the CPU.
		// m_ps_sel.fmt = 0; // removed as an optimization
		// m_ps_sel.aem = 0; // removed as an optimization
	}

	if (m_context->TEX0.TFX == TFX_MODULATE && m_vt.m_eq.rgba == 0xFFFF && m_vt.m_min.c.eq(GSVector4i(128)))
	{
		// Micro optimization that reduces GPU load (removes 5 instructions on the FS program)
		m_ps_sel.tfx = TFX_DECAL;
	}
	else
	{
		m_ps_sel.tfx = m_context->TEX0.TFX;
	}

	m_ps_sel.tcc = m_context->TEX0.TCC;

	m_ps_sel.ltf = bilinear && shader_emulated_sampler;

	m_ps_sel.spritehack = tex->m_spritehack_t;
	m_ps_sel.point_sampler = !bilinear || shader_emulated_sampler;

	GSVector4 TextureScale = GSVector4(0.0625f) / WH.xyxy();
	vs_cb.Texture_Scale_Offset.x = TextureScale.x;
	vs_cb.Texture_Scale_Offset.y = TextureScale.y;

	if (PRIM->FST)
	{
		//Maybe better?
		//vs_cb.TextureScale = GSVector4(1.0f / 16) * GSVector4(tex->m_texture->GetScale()).xyxy() / WH.zwzw();
		m_ps_sel.fst = 1;
	}

	ps_cb.WH = WH;
	ps_cb.HalfTexel = GSVector4(-0.5f, 0.5f).xxyy() / WH.zwzw();
	if (complex_wms_wmt)
	{
		ps_cb.MskFix = GSVector4i(m_context->CLAMP.MINU, m_context->CLAMP.MINV, m_context->CLAMP.MAXU, m_context->CLAMP.MAXV);
		ps_cb.MinMax = GSVector4(ps_cb.MskFix) / WH.xyxy();
	}

	// TC Offset Hack
	m_ps_sel.tcoffsethack = m_userhacks_tcoffset;
	ps_cb.TC_OffsetHack = GSVector4(m_userhacks_tcoffset_x, m_userhacks_tcoffset_y).xyxy() / WH.xyxy();

	// Must be done after all coordinates math
	if (m_context->HasFixedTEX0() && !PRIM->FST)
	{
		m_ps_sel.invalid_tex0 = 1;
		// Use invalid size to denormalize ST coordinate
		ps_cb.WH.x = (float)(1 << m_context->stack.TEX0.TW);
		ps_cb.WH.y = (float)(1 << m_context->stack.TEX0.TH);
	}

	// Only enable clamping in CLAMP mode. REGION_CLAMP will be done manually in the shader
	m_ps_ssel.tau = (wms != CLAMP_CLAMP);
	m_ps_ssel.tav = (wmt != CLAMP_CLAMP);
	m_ps_ssel.ltf = bilinear && !shader_emulated_sampler;
}

void GSRendererDX11::ResetStates()
{
	m_vs_sel.key = 0;
	m_gs_sel.key = 0;
	m_ps_sel.key = 0;

	m_ps_ssel.key  = 0;
	m_om_bsel.key  = 0;
	m_om_dssel.key = 0;
}

void GSRendererDX11::DrawPrims(GSTexture* rt, GSTexture* ds, GSTextureCache::Source* tex)
{
	GSTexture* hdr_rt = NULL;

	const GSVector2i& rtsize = ds ? ds->GetSize()  : rt->GetSize();
	const GSVector2& rtscale = ds ? ds->GetScale() : rt->GetScale();

	bool DATE = m_context->TEST.DATE && m_context->FRAME.PSM != PSM_PSMCT24;
	bool DATE_one = false;

	bool ate_first_pass = m_context->TEST.DoFirstPass();
	bool ate_second_pass = m_context->TEST.DoSecondPass();

	ResetStates();
	vs_cb.Texture_Scale_Offset = GSVector4(0.0f);

	ASSERT(m_dev != NULL);
	GSDevice11* dev = (GSDevice11*)m_dev;

	// HLE implementation of the channel selection effect
	//
	// Warning it must be done at the begining because it will change the vertex list
	EmulateChannelShuffle(&rt, tex);

	// Upscaling hack to avoid various line/grid issues
	MergeSprite(tex);

	EmulateTextureShuffleAndFbmask();

	// DATE: selection of the algorithm.

	if (DATE)
	{
		if (m_texture_shuffle)
		{
			// Direct3D doesn't support DATE_GL45 on m_texture_shuffle so keep using the old method.
			// Let's leave the check in to ensure the next code cases are hit properly.
			// fprintf(stderr, "Slow DATE with alpha %d-%d not supported on texture shuffle\n", m_vt.m_alpha.min, m_vt.m_alpha.max);
		}
		else if (m_om_bsel.wa && !m_context->TEST.ATE)
		{
			// Performance note: check alpha range with GetAlphaMinMax()
			GetAlphaMinMax();
			if (m_context->TEST.DATM && m_vt.m_alpha.max < 128)
			{
				// Only first pixel (write 0) will pass (alpha is 1)
				// fprintf(stderr, "Fast DATE with alpha %d-%d\n", m_vt.m_alpha.min, m_vt.m_alpha.max);
				DATE_one = true;
			}
			else if (!m_context->TEST.DATM && m_vt.m_alpha.min >= 128)
			{
				// Only first pixel (write 1) will pass (alpha is 0)
				// fprintf(stderr, "Fast DATE with alpha %d-%d\n", m_vt.m_alpha.min, m_vt.m_alpha.max);
				DATE_one = true;
			}
			else if ((m_vt.m_primclass == GS_SPRITE_CLASS /*&& m_drawlist.size() < 50*/) || (m_index.tail < 100))
			{
				// Direct3D doesn't support Slow DATE_GL45.
				// Let's make sure it triggers this check and continues to use the old DATE code to avoid any issues with Fast Accurate Date.
				// m_drawlist.size() isn't supported on D3D so there will be more games hitting this code path,
				// it should be fine with regular DATE since originally it ran with it anyway.
				// Note: Potentially Alpha Stencil might emulate SLOW DATE to some degree. Perhaps some of the code can be implemented here.
				// fprintf(stderr, "Slow DATE with alpha %d-%d not supported\n", m_vt.m_alpha.min, m_vt.m_alpha.max);
			}
			else if (!UserHacks_AlphaStencil)
			{
				if (m_accurate_date)
				{
					// fprintf(stderr, "Fast Accurate DATE with alpha %d-%d\n", m_vt.m_alpha.min, m_vt.m_alpha.max);
					DATE_one = true;
				}
				else
				{
					// DATE is already true, no need for another check.
					// fprintf(stderr, "Inaccurate DATE with alpha %d-%d\n", m_vt.m_alpha.min, m_vt.m_alpha.max);
				}
			}
		}
		else if (!m_om_bsel.wa && !m_context->TEST.ATE)
		{
			// TODO: is it legal ? Likely but it need to be tested carefully.
		}
	}

	// Blend

	if (!IsOpaque())
	{
		m_om_bsel.abe = PRIM->ABE || PRIM->AA1 && m_vt.m_primclass == GS_LINE_CLASS;

		m_om_bsel.a = m_context->ALPHA.A;
		m_om_bsel.b = m_context->ALPHA.B;
		m_om_bsel.c = m_context->ALPHA.C;
		m_om_bsel.d = m_context->ALPHA.D;

		if (m_env.PABE.PABE)
		{
			if (m_om_bsel.a == 0 && m_om_bsel.b == 1 && m_om_bsel.c == 0 && m_om_bsel.d == 1)
			{
				// this works because with PABE alpha blending is on when alpha >= 0x80, but since the pixel shader
				// cannot output anything over 0x80 (== 1.0) blending with 0x80 or turning it off gives the same result

				m_om_bsel.abe = 0;
			}
			else
			{
				//Breath of Fire Dragon Quarter triggers this in battles. Graphics are fine though.
				//ASSERT(0);
			}
		}
	}

	if (m_ps_sel.dfmt == 1)
	{
		// Disable writing of the alpha channel
		m_om_bsel.wa = 0;
	}

	if (DATE)
	{
		GSVector4i dRect = ComputeBoundingBox(rtscale, rtsize);

		GSVector4 src = GSVector4(dRect) / GSVector4(rtsize.x, rtsize.y).xyxy();
		GSVector4 dst = src * 2.0f - 1.0f;

		GSVertexPT1 vertices[] =
		{
			{GSVector4(dst.x, -dst.y, 0.5f, 1.0f), GSVector2(src.x, src.y)},
			{GSVector4(dst.z, -dst.y, 0.5f, 1.0f), GSVector2(src.z, src.y)},
			{GSVector4(dst.x, -dst.w, 0.5f, 1.0f), GSVector2(src.x, src.w)},
			{GSVector4(dst.z, -dst.w, 0.5f, 1.0f), GSVector2(src.z, src.w)},
		};

		dev->SetupDATE(rt, ds, vertices, m_context->TEST.DATM);
	}

	//

	bool hdr_colclip = m_env.COLCLAMP.CLAMP == 0 && rt;
	if (hdr_colclip)
	{
		// fprintf(stderr, "COLCLIP HDR mode ENABLED\n");
		GSVector4 dRect(ComputeBoundingBox(rtscale, rtsize));
		GSVector4 sRect = dRect / GSVector4(rtsize.x, rtsize.y).xyxy();
		hdr_rt = dev->CreateRenderTarget(rtsize.x, rtsize.y, DXGI_FORMAT_R32G32B32A32_FLOAT);
		// Warning: StretchRect must be called before BeginScene otherwise
		// vertices will be overwritten. Trust me you don't want to do that.
		dev->StretchRect(rt, sRect, hdr_rt, dRect, ShaderConvert_COPY, false);
	}

	dev->BeginScene();

	// om

	EmulateZbuffer();

	// vs

	m_vs_sel.tme = PRIM->TME;
	m_vs_sel.fst = PRIM->FST;

	// FIXME D3D11 and GL support half pixel center. Code could be easier!!!
	float sx = 2.0f * rtscale.x / (rtsize.x << 4);
	float sy = 2.0f * rtscale.y / (rtsize.y << 4);
	float ox = (float)(int)m_context->XYOFFSET.OFX;
	float oy = (float)(int)m_context->XYOFFSET.OFY;
	float ox2 = -1.0f / rtsize.x;
	float oy2 = -1.0f / rtsize.y;

	//This hack subtracts around half a pixel from OFX and OFY.
	//
	//The resulting shifted output aligns better with common blending / corona / blurring effects,
	//but introduces a few bad pixels on the edges.

	if (rt && rt->LikelyOffset && m_userHacks_HPO == 1)
	{
		ox2 *= rt->OffsetHack_modx;
		oy2 *= rt->OffsetHack_mody;
	}

	vs_cb.VertexScale  = GSVector4(sx, -sy, ldexpf(1, -32), 0.0f);
	vs_cb.VertexOffset = GSVector4(ox * sx + ox2 + 1, -(oy * sy + oy2 + 1), 0.0f, -1.0f);
	// END of FIXME

	// gs

	m_gs_sel.iip = PRIM->IIP;
	m_gs_sel.prim = m_vt.m_primclass;

	// ps

	if (DATE)
	{
		m_om_dssel.date = 1;
		if (DATE_one)
		{
			m_om_dssel.date_one = 1;
		}
	}

	m_ps_sel.clr1 = m_om_bsel.IsCLR1();
	m_ps_sel.fba = m_context->FBA.FBA;

	// FIXME: Purge aout with AlphaHack when FbMask emulation is added.
	if (m_ps_sel.shuffle)
	{
		m_ps_sel.aout = 0;
	}
	else
	{
		m_ps_sel.aout = UserHacks_AlphaHack || (m_context->FRAME.FBMSK & 0xff000000) == 0x7f000000;
	}
	// END OF FIXME

	if (PRIM->FGE)
	{
		m_ps_sel.fog = 1;

		GSVector4 fc = GSVector4::rgba32(m_env.FOGCOL.u32[0]);
#if _M_SSE >= 0x401
		// Blend AREF to avoid to load a random value for alpha (dirty cache)
		ps_cb.FogColor_AREF = fc.blend32<8>(ps_cb.FogColor_AREF) / 255;
#else
		ps_cb.FogColor_AREF = fc / 255;
#endif
	}

	// Warning must be done after EmulateZbuffer
	// Depth test is always true so it can be executed in 2 passes (no order required) unlike color.
	// The idea is to compute first the color which is independent of the alpha test. And then do a 2nd
	// pass to handle the depth based on the alpha test.
	bool ate_RGBA_then_Z = false;
	bool ate_RGB_then_ZA = false;
	if (ate_first_pass & ate_second_pass)
	{
		// fprintf(stdout, "Complex Alpha Test\n");
		bool commutative_depth = (m_om_dssel.ztst == ZTST_GEQUAL && m_vt.m_eq.z) || (m_om_dssel.ztst == ZTST_ALWAYS);
		bool commutative_alpha = (m_context->ALPHA.C != 1); // when either Alpha Src or a constant

		ate_RGBA_then_Z = (m_context->TEST.AFAIL == AFAIL_FB_ONLY) & commutative_depth;
		ate_RGB_then_ZA = (m_context->TEST.AFAIL == AFAIL_RGB_ONLY) & commutative_depth & commutative_alpha;
	}

	if (ate_RGBA_then_Z)
	{
		// fprintf(stdout, "Alternate ATE handling: ate_RGBA_then_Z\n");
		// Render all color but don't update depth
		// ATE is disabled here
		m_om_dssel.zwe = false;
	}
	else if (ate_RGB_then_ZA)
	{
		// fprintf(stdout, "Alternate ATE handling: ate_RGB_then_ZA\n");
		// Render RGB color but don't update depth/alpha
		// ATE is disabled here
		m_om_dssel.zwe = false;
		m_om_bsel.wa = false;
	}
	else
	{
		EmulateAtst(1, tex);
	}

	// Destination alpha pseudo stencil hack: use a stencil operation combined with an alpha test
	// to only draw pixels which would cause the destination alpha test to fail in the future once.
	// Unfortunately this also means only drawing those pixels at all, which is why this is a hack.
	if (UserHacks_AlphaStencil && DATE && !DATE_one && m_om_bsel.wa && !m_context->TEST.ATE)
	{
		// fprintf(stderr, "Alpha Stencil detected\n");
		if (!m_context->FBA.FBA)
		{
			if (m_context->TEST.DATM == 0)
				m_ps_sel.atst = 2; // >=
			else
			{
				if (tex && tex->m_spritehack_t)
					m_ps_sel.atst = 0; // <
				else
					m_ps_sel.atst = 1; // <
			}
			ps_cb.FogColor_AREF.a = (float)0x80;
		}
		if (!(m_context->FBA.FBA && m_context->TEST.DATM == 1))
			m_om_dssel.date_one = 1;
	}

	if (tex)
	{
		EmulateTextureSampler(tex);
	}
	else
	{
		m_ps_sel.tfx = 4;
	}

	if (m_game.title == CRC::ICO)
	{
		GSVertex* v = &m_vertex.buff[0];
		const GSVideoMode mode = GetVideoMode();
		if (tex && m_vt.m_primclass == GS_SPRITE_CLASS && m_vertex.next == 2 && PRIM->ABE && // Blend texture
				((v[1].U == 8200 && v[1].V == 7176 && mode == GSVideoMode::NTSC) || // at display resolution 512x448
				(v[1].U == 8200 && v[1].V == 8200 && mode == GSVideoMode::PAL)) && // at display resolution 512x512
				tex->m_TEX0.PSM == PSM_PSMT8H) // i.e. read the alpha channel of a 32 bits texture
		{
			// Note potentially we can limit to TBP0:0x2800

			// Depth buffer was moved so GSdx will invalide it which means a
			// downscale. ICO uses the MSB depth bits as the texture alpha
			// channel.  However this depth of field effect requires
			// texel:pixel mapping accuracy.
			//
			// Use an HLE shader to sample depth directly as the alpha channel

			// OutputDebugString("ICO HLE");

			m_ps_sel.depth_fmt = 1;
			m_ps_sel.channel = ChannelFetch_BLUE;

			dev->PSSetShaderResource(4, ds);

			if (!tex->m_palette)
			{
				uint16 pal = GSLocalMemory::m_psm[tex->m_TEX0.PSM].pal;
				m_tc->AttachPaletteToSource(tex, pal, true);
			}
		}
	}

	// rs
	const GSVector4& hacked_scissor = m_channel_shuffle ? GSVector4(0, 0, 1024, 1024) : m_context->scissor.in;
	GSVector4i scissor = GSVector4i(GSVector4(rtscale).xyxy() * hacked_scissor).rintersect(GSVector4i(rtsize).zwxy());

	if (hdr_rt)
		dev->OMSetRenderTargets(hdr_rt, ds, &scissor);
	else
		dev->OMSetRenderTargets(rt, ds, &scissor);

	dev->PSSetShaderResource(0, tex ? tex->m_texture : NULL);
	dev->PSSetShaderResource(1, tex ? tex->m_palette : NULL);

	SetupIA(sx, sy);

	uint8 afix = m_context->ALPHA.FIX;
	dev->SetupOM(m_om_dssel, m_om_bsel, afix);
	dev->SetupVS(m_vs_sel, &vs_cb);
	dev->SetupGS(m_gs_sel, &gs_cb);
	dev->SetupPS(m_ps_sel, &ps_cb, m_ps_ssel);

	// draw

	if (ate_first_pass)
	{
		dev->DrawIndexedPrimitive();
	}

	if (ate_second_pass)
	{
		ASSERT(!m_env.PABE.PABE);

		if (ate_RGBA_then_Z | ate_RGB_then_ZA)
		{
			// Enable ATE as first pass to update the depth
			// of pixels that passed the alpha test
			EmulateAtst(1, tex);
		}
		else
		{
			// second pass will process the pixels that failed
			// the alpha test
			EmulateAtst(2, tex);
		}

		dev->SetupPS(m_ps_sel, &ps_cb, m_ps_ssel);

		bool z = m_om_dssel.zwe;
		bool r = m_om_bsel.wr;
		bool g = m_om_bsel.wg;
		bool b = m_om_bsel.wb;
		bool a = m_om_bsel.wa;

		switch(m_context->TEST.AFAIL)
		{
			case AFAIL_KEEP: z = r = g = b = a = false; break; // none
			case AFAIL_FB_ONLY: z = false; break; // rgba
			case AFAIL_ZB_ONLY: r = g = b = a = false; break; // z
			case AFAIL_RGB_ONLY: z = a = false; break; // rgb
			default: __assume(0);
		}

		// Depth test should be disabled when depth writes are masked and similarly, Alpha test must be disabled
		// when writes to all of the alpha bits in the Framebuffer are masked.
		if (ate_RGBA_then_Z)
		{
			z = !m_context->ZBUF.ZMSK;
			r = g = b = a = false;
		}
		else if (ate_RGB_then_ZA)
		{
			z = !m_context->ZBUF.ZMSK;
			a = (m_context->FRAME.FBMSK & 0xFF000000) != 0xFF000000;
			r = g = b = false;
		}

		if (z || r || g || b || a)
		{
			m_om_dssel.zwe = z;
			m_om_bsel.wr = r;
			m_om_bsel.wg = g;
			m_om_bsel.wb = b;
			m_om_bsel.wa = a;

			dev->SetupOM(m_om_dssel, m_om_bsel, afix);

			dev->DrawIndexedPrimitive();
		}
	}

	dev->EndScene();

	// Warning: EndScene must be called before StretchRect otherwise
	// vertices will be overwritten. Trust me you don't want to do that.
	if (hdr_rt)
	{
		GSVector4 dRect(ComputeBoundingBox(rtscale, rtsize));
		GSVector4 sRect = dRect / GSVector4(rtsize.x, rtsize.y).xyxy();
		dev->StretchRect(hdr_rt, sRect, rt, dRect, ShaderConvert_MOD_256, false);

		dev->Recycle(hdr_rt);
	}
}
