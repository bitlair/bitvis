/*
 * bitvis
 * Copyright (C) Bob 2012
 * 
 * bitvis is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * 
 * bitvis is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License along
 * with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef FFT_H
#define FFT_H

#include <complex.h>
#include <fftw3.h>

class Cfft
{
  public:
    Cfft();
    ~Cfft();

    void Allocate(unsigned int size);
    void Free();
    void ApplyWindow();
    void AddSample(float sample);
    
    float*         m_inbuf;
    float*         m_fftin;
    float*         m_window;
    fftwf_complex* m_outbuf;
    unsigned int   m_bufsize;
    fftwf_plan     m_plan;

  private:

};
#endif //FFT_H
