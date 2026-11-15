/* DLSS through NVIDIA Streamline (dlss.c), loaded at run time; what the Direct3D 12 renderer calls */
#ifndef DX_DLSS_H
#define DX_DLSS_H

int sl_files_here(void);
int sl_start(void);
int sl_device(ID3D12Device **dev);
int sl_factory(IDXGIFactory4 **fac);
int sl_run(ID3D12GraphicsCommandList *cl, ID3D12Resource *color, ID3D12Resource *depth, ID3D12Resource *mv, ID3D12Resource *out, int rw, int rh, int uw, int uh, float jx, float jy, int reset, float fov);
unsigned sl_phases(void);
int sl_state(void);
void sl_stop(void);

#endif
