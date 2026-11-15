/* FSR 3.1 (fsr3.c): AMD's temporal upscaler, loaded at run time; what the Direct3D 12 renderer calls */
#ifndef DX_FSR3_H
#define DX_FSR3_H

int fx_load(void);
int fx_run(ID3D12Device *dev, ID3D12GraphicsCommandList *cl, ID3D12Resource *color, ID3D12Resource *depth, ID3D12Resource *mv, ID3D12Resource *out, int rw, int rh, int uw, int uh, float jx, float jy, int reset, float fov, float ms);
int fx_loaded(void);
void fx_jitter_now(unsigned index, float *x, float *y);

#endif
