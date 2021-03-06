// license:GPL-2.0+
// copyright-holders:Couriersud
/*
 * nld_truthtable.c
 *
 */

#include "nld_truthtable.h"
#include "../plists.h"

int truthtable_desc_t::count_bits(UINT32 v)
{
	int ret = 0;
	for (; v != 0; v = v >> 1)
	{
		if (v & 1)
			ret++;
	}
	return ret;
}

UINT32 truthtable_desc_t::set_bits(UINT32 v, UINT32 b)
{
	UINT32 ret = 0;
	int i = 0;
	for (; v != 0; v = v >> 1)
	{
		if (v & 1)
		{
			ret |= ((b&1)<<i);
			b = b >> 1;
		}
		i++;
	}
	return ret;
}

UINT32 truthtable_desc_t::get_ignored_simple(UINT32 i)
{
	UINT32 m_enable = 0;
	for (UINT32 j=0; j<m_size; j++)
	{
		if (m_outs[j] != m_outs[i])
		{
			m_enable |= (i ^ j);
		}
	}
	return m_enable ^ (m_size - 1);
}

UINT32 truthtable_desc_t::get_ignored_extended(UINT32 i)
{
	// Determine all inputs which may be ignored ...
	UINT32 nign = 0;
	for (int j=0; j<m_NI; j++)
	{
		if (m_outs[i] == m_outs[i ^ (1 << j)])
			nign |= (1<<j);
	}
	/* Check all permutations of ign
	 * We have to remove those where the ignored inputs
	 * may change the output
	 */
	int bits = (1<<count_bits(nign));
	plinearlist_t<int> t;
	t.set_count(bits);

	for (UINT32 j=1; j<bits; j++)
	{
		UINT32 tign = set_bits(nign, j);
		t[j] = 0;
		int bitsk=(1<<count_bits(tign));
		for (UINT32 k=0; k<bitsk; k++)
		{
			UINT32 b=set_bits(tign, k);
			if (m_outs[i] != m_outs[(i & ~tign) | b])
			{
				t[j] = 1;
				break;
			}
		}
	}
	UINT32 jb=0;
	UINT32 jm=0;
	for (UINT32 j=1; j<bits; j++)
	{
		int nb = count_bits(j);
		if ((t[j] == 0)&& (nb>jb))
		{
			jb = nb;
			jm = j;
		}
	}
	return set_bits(nign, jm);
}

// ----------------------------------------------------------------------------------------
// desc
// ----------------------------------------------------------------------------------------

ATTR_COLD void truthtable_desc_t::help(int cur, nl_util::pstring_list list,
		UINT64 state,UINT16 val, UINT8 *timing_index)
{
	pstring elem = list[cur].trim();
	int start = 0;
	int end = 0;

	if (elem.equals("0"))
	{
		start = 0;
		end = 0;
	}
	else if (elem.equals("1"))
	{
		start = 1;
		end = 1;
	}
	else if (elem.equals("X"))
	{
		start = 0;
		end = 1;
	}
	else
		nl_assert_always(false, "unknown input value (not 0, 1, or X)");
	for (int i = start; i <= end; i++)
	{
		const UINT64 nstate = state | (i << cur);

		if (cur < m_num_bits - 1)
		{
			help(cur + 1, list, nstate, val, timing_index);
		}
		else
		{
			// cutoff previous inputs and outputs for ignore
			if (m_outs[nstate] != -1 &&  m_outs[nstate] != val)
				nl_fatalerror("Error in truthtable: State %04x already set, %d != %d\n",
						(UINT32) nstate, m_outs[nstate], val);
			m_outs[nstate] = val;
			for (int j=0; j<m_NO; j++)
				m_timing[nstate * m_NO + j] = timing_index[j];
		}
	}
}

ATTR_COLD void truthtable_desc_t::setup(const char **truthtable, UINT32 disabled_ignore)
{
	if (*m_initialized)
		return;

	pstring ttline = pstring(truthtable[0]);
	truthtable++;
	ttline = pstring(truthtable[0]);
	truthtable++;

	for (int j=0; j < m_size; j++)
		m_outs[j] = -1;

	for (int j=0; j < 16; j++)
		m_timing_nt[j] = netlist_time::zero;

	while (!ttline.equals(""))
	{
		nl_util::pstring_list io = nl_util::split(ttline,"|");
		// checks
		nl_assert_always(io.count() == 3, "io.count mismatch");
		nl_util::pstring_list inout = nl_util::split(io[0], ",");
		nl_assert_always(inout.count() == m_num_bits, "number of bits not matching");
		nl_util::pstring_list out = nl_util::split(io[1], ",");
		nl_assert_always(out.count() == m_NO, "output count not matching");
		nl_util::pstring_list times = nl_util::split(io[2], ",");
		nl_assert_always(times.count() == m_NO, "timing count not matching");

		UINT16 val = 0;
		plinearlist_t<UINT8> tindex;
		tindex.set_count(m_NO);

		for (int j=0; j<m_NO; j++)
		{
			pstring outs = out[j].trim();
			if (outs.equals("1"))
				val = val | (1 << j);
			else
				nl_assert_always(outs.equals("0"), "Unknown value (not 0 or 1");
			netlist_time t = netlist_time::from_nsec(times[j].trim().as_long());
			int k=0;
			while (m_timing_nt[k] != netlist_time::zero && m_timing_nt[k] != t)
				k++;
			m_timing_nt[k] = t;
			tindex[j] = k;
		}

		help(0, inout, 0 , val, tindex);
		ttline = pstring(truthtable[0]);
		truthtable++;
	}

	// determine ignore
	plinearlist_t<UINT32> ign;
	ign.set_count(m_size);

	for (int j=0; j < m_size; j++)
		ign[j] = -1;

	for (UINT32 i=0; i<m_size; i++)
	{
		if (ign[i] == -1)
		{
			int tign;
			if (0)
			{
				tign = get_ignored_simple(i);
				ign[i] = tign;
			}
			else
			{
				tign = get_ignored_extended(i);

				ign[i] = tign;
				/* don't need to recalculate similar ones */
				int bitsk=(1<<count_bits(tign));
				for (UINT32 k=0; k<bitsk; k++)
				{
					UINT32 b=set_bits(tign, k);
					ign[(i & ~tign) | b] = tign;
				}
			}
		}
	}
	for (int i=0; i<m_size; i++)
	{
		if (m_outs[i] == -1)
			nl_fatalerror("truthtable: found element not set %04x\n", i);
		m_outs[i] |= ((ign[i] & ~disabled_ignore)  << m_NO);
	}
	*m_initialized = true;

}


