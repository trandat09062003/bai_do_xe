// ==============================================================================
// Script Name: AutoPlace_Baidoxe.pas
// Project    : Bai Do Xe Thong Minh ESP32 (Smart Parking ESP32)
// Purpose    : Tu dong xep 40 linh kien vao toa do chuan chi trong Altium Designer
// Updated    : Khop 100% Schematic Annotation moi nhat (R1..R6, C12, C13, L1, M1)
// Author     : Antigravity Senior PCB Design Engineer
// Usage      : Trong Altium Designer, mo baidoxe.PcbDoc -> File -> Run Script...
//              Chon AutoPlace_Baidoxe.pas -> Chon AutoPlace_100x100 hoac AutoPlace_125x95
// ==============================================================================

Procedure AutoPlace_100x100;
Var
    Board     : IPCB_Board;
    Iterator  : IPCB_BoardIterator;
    Comp      : IPCB_Component;
    Desig     : String;
    Count     : Integer;
Begin
    Board := PCBServer.GetCurrentPCBBoard;
    If Board = Nil Then Begin
        ShowError('Vui long mo file baidoxe.PcbDoc truoc khi chay script!');
        Exit;
    End;

    PCBServer.PreProcess;
    Iterator := Board.BoardIterator_Create;
    Iterator.AddFilter_ObjectSet(MkSet(eComponentObject));
    Iterator.AddFilter_LayerSet(AllLayers);

    Count := 0;
    Comp := Iterator.FirstPCBObject;
    While Comp <> Nil Do Begin
        Desig := UpperCase(Trim(Comp.SourceDesignator));

        // 1. KHOI NGUON 12V VAO & BUCK LM2576 (GOC TREN PHAI)
        If (Desig = 'J1') Then Begin
            Comp.X := MMsToCoord(92.5); Comp.Y := MMsToCoord(92.5); Comp.Rotation := 0; Inc(Count);
        End
        Else If (Desig = 'F1') Then Begin
            Comp.X := MMsToCoord(84.0); Comp.Y := MMsToCoord(90.0); Comp.Rotation := 0; Inc(Count);
        End
        Else If (Desig = 'D1') Then Begin
            Comp.X := MMsToCoord(77.5); Comp.Y := MMsToCoord(90.0); Comp.Rotation := 0; Inc(Count);
        End
        Else If (Desig = 'C1') Then Begin
            Comp.X := MMsToCoord(71.0); Comp.Y := MMsToCoord(90.0); Comp.Rotation := 0; Inc(Count);
        End
        Else If (Desig = 'C2') Then Begin
            Comp.X := MMsToCoord(71.0); Comp.Y := MMsToCoord(85.5); Comp.Rotation := 0; Inc(Count);
        End
        Else If (Desig = 'U1') Then Begin
            Comp.X := MMsToCoord(61.0); Comp.Y := MMsToCoord(89.0); Comp.Rotation := 0; Inc(Count);
        End
        Else If (Desig = 'D2') Then Begin
            Comp.X := MMsToCoord(61.0); Comp.Y := MMsToCoord(81.0); Comp.Rotation := 90; Inc(Count);
        End
        Else If (Desig = 'L1') Or (Desig = 'L?') Then Begin
            Comp.X := MMsToCoord(51.5); Comp.Y := MMsToCoord(89.0); Comp.Rotation := 0; Inc(Count);
        End
        Else If (Desig = 'C8') Then Begin
            Comp.X := MMsToCoord(51.5); Comp.Y := MMsToCoord(81.0); Comp.Rotation := 0; Inc(Count);
        End
        Else If (Desig = 'C9') Then Begin
            Comp.X := MMsToCoord(51.5); Comp.Y := MMsToCoord(76.0); Comp.Rotation := 0; Inc(Count);
        End
        Else If (Desig = 'R1') Then Begin
            Comp.X := MMsToCoord(45.5); Comp.Y := MMsToCoord(80.5); Comp.Rotation := 0; Inc(Count);
        End

        // 2. KHOI LDO AMS1117-3.3V
        Else If (Desig = 'AMS1') Then Begin
            Comp.X := MMsToCoord(76.5); Comp.Y := MMsToCoord(77.5); Comp.Rotation := 0; Inc(Count);
        End
        Else If (Desig = 'C6') Then Begin
            Comp.X := MMsToCoord(70.0); Comp.Y := MMsToCoord(77.5); Comp.Rotation := 0; Inc(Count);
        End
        Else If (Desig = 'C7') Then Begin
            Comp.X := MMsToCoord(70.0); Comp.Y := MMsToCoord(73.0); Comp.Rotation := 0; Inc(Count);
        End
        Else If (Desig = 'C4') Then Begin
            Comp.X := MMsToCoord(84.0); Comp.Y := MMsToCoord(77.5); Comp.Rotation := 0; Inc(Count);
        End
        Else If (Desig = 'C5') Then Begin
            Comp.X := MMsToCoord(84.0); Comp.Y := MMsToCoord(73.0); Comp.Rotation := 0; Inc(Count);
        End

        // 3. KHOI DONG CO BUOC X & Z (A4988)
        Else If (Desig = 'U2') Then Begin
            Comp.X := MMsToCoord(13.5); Comp.Y := MMsToCoord(72.5); Comp.Rotation := 0; Inc(Count);
        End
        Else If (Desig = 'C12') Then Begin
            Comp.X := MMsToCoord(18.5); Comp.Y := MMsToCoord(87.0); Comp.Rotation := 0; Inc(Count);
        End
        Else If (Desig = 'H1') Then Begin
            Comp.X := MMsToCoord(10.0); Comp.Y := MMsToCoord(88.5); Comp.Rotation := 0; Inc(Count);
        End
        Else If (Desig = 'U3') Then Begin
            Comp.X := MMsToCoord(32.5); Comp.Y := MMsToCoord(72.5); Comp.Rotation := 0; Inc(Count);
        End
        Else If (Desig = 'C13') Then Begin
            Comp.X := MMsToCoord(37.5); Comp.Y := MMsToCoord(87.0); Comp.Rotation := 0; Inc(Count);
        End
        Else If (Desig = 'H2') Then Begin
            Comp.X := MMsToCoord(29.0); Comp.Y := MMsToCoord(88.5); Comp.Rotation := 0; Inc(Count);
        End

        // 4. KHOI VI DIEU KHIEN TRUNG TAM ESP32
        Else If (Desig = 'ESP1') Then Begin
            Comp.X := MMsToCoord(20.0); Comp.Y := MMsToCoord(32.0); Comp.Rotation := 0; Inc(Count);
        End

        // 5. KHOI CAM BIEN, RFID, LCD, SERVO & NGOAI VI
        Else If (Desig = 'LCD1') Then Begin
            Comp.X := MMsToCoord(47.0); Comp.Y := MMsToCoord(9.5);  Comp.Rotation := 0; Inc(Count);
        End
        Else If (Desig = 'M1') Or (Desig = 'M?') Then Begin
            Comp.X := MMsToCoord(83.0); Comp.Y := MMsToCoord(46.0); Comp.Rotation := 0; Inc(Count);
        End
        Else If (Desig = 'IR1') Or (Desig = '*') Then Begin
            Comp.X := MMsToCoord(89.5); Comp.Y := MMsToCoord(9.5);  Comp.Rotation := 0; Inc(Count);
        End
        Else If (Desig = 'B3') Then Begin
            Comp.X := MMsToCoord(40.5); Comp.Y := MMsToCoord(55.5); Comp.Rotation := 0; Inc(Count);
        End
        Else If (Desig = 'R2') Then Begin
            Comp.X := MMsToCoord(45.5); Comp.Y := MMsToCoord(55.5); Comp.Rotation := 0; Inc(Count);
        End
        Else If (Desig = 'C10') Then Begin
            Comp.X := MMsToCoord(40.5); Comp.Y := MMsToCoord(50.5); Comp.Rotation := 0; Inc(Count);
        End
        Else If (Desig = 'B4') Then Begin
            Comp.X := MMsToCoord(40.5); Comp.Y := MMsToCoord(44.5); Comp.Rotation := 0; Inc(Count);
        End
        Else If (Desig = 'R3') Then Begin
            Comp.X := MMsToCoord(45.5); Comp.Y := MMsToCoord(44.5); Comp.Rotation := 0; Inc(Count);
        End
        Else If (Desig = 'C11') Then Begin
            Comp.X := MMsToCoord(40.5); Comp.Y := MMsToCoord(39.5); Comp.Rotation := 0; Inc(Count);
        End
        Else If (Desig = 'B1') Then Begin
            Comp.X := MMsToCoord(40.5); Comp.Y := MMsToCoord(33.5); Comp.Rotation := 0; Inc(Count);
        End
        Else If (Desig = 'R4') Then Begin
            Comp.X := MMsToCoord(45.5); Comp.Y := MMsToCoord(33.5); Comp.Rotation := 0; Inc(Count);
        End
        Else If (Desig = 'C3') Then Begin
            Comp.X := MMsToCoord(40.5); Comp.Y := MMsToCoord(28.5); Comp.Rotation := 0; Inc(Count);
        End
        Else If (Desig = 'L2') Then Begin
            Comp.X := MMsToCoord(55.0); Comp.Y := MMsToCoord(57.0); Comp.Rotation := 0; Inc(Count);
        End
        Else If (Desig = 'R5') Then Begin
            Comp.X := MMsToCoord(55.0); Comp.Y := MMsToCoord(53.5); Comp.Rotation := 0; Inc(Count);
        End
        Else If (Desig = 'B2') Then Begin
            Comp.X := MMsToCoord(63.0); Comp.Y := MMsToCoord(55.0); Comp.Rotation := 0; Inc(Count);
        End
        Else If (Desig = 'Q1') Then Begin
            Comp.X := MMsToCoord(63.5); Comp.Y := MMsToCoord(48.5); Comp.Rotation := 0; Inc(Count);
        End
        Else If (Desig = 'R6') Then Begin
            Comp.X := MMsToCoord(63.5); Comp.Y := MMsToCoord(45.0); Comp.Rotation := 0; Inc(Count);
        End;

        Comp := Iterator.NextPCBObject;
    End;
    Board.BoardIterator_Destroy(Iterator);
    PCBServer.PostProcess;

    ResetParameters;
    AddStringParameter('Action', 'All');
    RunProcess('PCB:Zoom');

    ShowInfo('Thanh cong! Da sap xep ' + IntToStr(Count) + ' linh kien theo Annotation moi vao vi tri chuan 100x100mm ($2 pool)!');
End;

Procedure AutoPlace_125x95;
Var
    Board     : IPCB_Board;
    Iterator  : IPCB_BoardIterator;
    Comp      : IPCB_Component;
    Desig     : String;
    Count     : Integer;
Begin
    Board := PCBServer.GetCurrentPCBBoard;
    If Board = Nil Then Begin
        ShowError('Vui long mo file baidoxe.PcbDoc truoc khi chay script!');
        Exit;
    End;

    PCBServer.PreProcess;
    Iterator := Board.BoardIterator_Create;
    Iterator.AddFilter_ObjectSet(MkSet(eComponentObject));
    Iterator.AddFilter_LayerSet(AllLayers);

    Count := 0;
    Comp := Iterator.FirstPCBObject;
    While Comp <> Nil Do Begin
        Desig := UpperCase(Trim(Comp.SourceDesignator));

        If (Desig = 'J1')   Then Begin Comp.X := MMsToCoord(112.0); Comp.Y := MMsToCoord(80.0); Comp.Rotation := 0; Inc(Count); End;
        If (Desig = 'F1')   Then Begin Comp.X := MMsToCoord(104.0); Comp.Y := MMsToCoord(80.0); Comp.Rotation := 0; Inc(Count); End;
        If (Desig = 'D1')   Then Begin Comp.X := MMsToCoord(98.0);  Comp.Y := MMsToCoord(80.0); Comp.Rotation := 0; Inc(Count); End;
        If (Desig = 'C1')   Then Begin Comp.X := MMsToCoord(92.0);  Comp.Y := MMsToCoord(80.0); Comp.Rotation := 0; Inc(Count); End;
        If (Desig = 'C2')   Then Begin Comp.X := MMsToCoord(92.5);  Comp.Y := MMsToCoord(74.5); Comp.Rotation := 0; Inc(Count); End;
        If (Desig = 'U1')   Then Begin Comp.X := MMsToCoord(81.0);  Comp.Y := MMsToCoord(80.0); Comp.Rotation := 0; Inc(Count); End;
        If (Desig = 'D2')   Then Begin Comp.X := MMsToCoord(81.0);  Comp.Y := MMsToCoord(71.5); Comp.Rotation := 90; Inc(Count); End;
        If (Desig = 'L1') Or (Desig = 'L?')   Then Begin Comp.X := MMsToCoord(73.0);  Comp.Y := MMsToCoord(80.0); Comp.Rotation := 0; Inc(Count); End;
        If (Desig = 'C8')   Then Begin Comp.X := MMsToCoord(73.0);  Comp.Y := MMsToCoord(71.5); Comp.Rotation := 0; Inc(Count); End;
        If (Desig = 'C9')   Then Begin Comp.X := MMsToCoord(73.0);  Comp.Y := MMsToCoord(66.0); Comp.Rotation := 0; Inc(Count); End;
        If (Desig = 'R1')   Then Begin Comp.X := MMsToCoord(68.0);  Comp.Y := MMsToCoord(70.0); Comp.Rotation := 0; Inc(Count); End;
        If (Desig = 'AMS1') Then Begin Comp.X := MMsToCoord(98.0);  Comp.Y := MMsToCoord(65.5); Comp.Rotation := 0; Inc(Count); End;
        If (Desig = 'C6')   Then Begin Comp.X := MMsToCoord(91.0);  Comp.Y := MMsToCoord(65.5); Comp.Rotation := 0; Inc(Count); End;
        If (Desig = 'C7')   Then Begin Comp.X := MMsToCoord(91.0);  Comp.Y := MMsToCoord(60.5); Comp.Rotation := 0; Inc(Count); End;
        If (Desig = 'C4')   Then Begin Comp.X := MMsToCoord(105.0); Comp.Y := MMsToCoord(65.5); Comp.Rotation := 0; Inc(Count); End;
        If (Desig = 'C5')   Then Begin Comp.X := MMsToCoord(105.0); Comp.Y := MMsToCoord(60.5); Comp.Rotation := 0; Inc(Count); End;
        If (Desig = 'U2')   Then Begin Comp.X := MMsToCoord(14.5);  Comp.Y := MMsToCoord(61.5); Comp.Rotation := 0; Inc(Count); End;
        If (Desig = 'C12')  Then Begin Comp.X := MMsToCoord(19.5);  Comp.Y := MMsToCoord(76.0); Comp.Rotation := 0; Inc(Count); End;
        If (Desig = 'H1')   Then Begin Comp.X := MMsToCoord(11.0);  Comp.Y := MMsToCoord(77.5); Comp.Rotation := 0; Inc(Count); End;
        If (Desig = 'U3')   Then Begin Comp.X := MMsToCoord(34.5);  Comp.Y := MMsToCoord(61.5); Comp.Rotation := 0; Inc(Count); End;
        If (Desig = 'C13')  Then Begin Comp.X := MMsToCoord(39.5);  Comp.Y := MMsToCoord(76.0); Comp.Rotation := 0; Inc(Count); End;
        If (Desig = 'H2')   Then Begin Comp.X := MMsToCoord(31.0);  Comp.Y := MMsToCoord(77.5); Comp.Rotation := 0; Inc(Count); End;
        If (Desig = 'ESP1') Then Begin Comp.X := MMsToCoord(21.0);  Comp.Y := MMsToCoord(24.0); Comp.Rotation := 0; Inc(Count); End;
        If (Desig = 'LCD1') Then Begin Comp.X := MMsToCoord(53.0);  Comp.Y := MMsToCoord(6.5);  Comp.Rotation := 0; Inc(Count); End;
        If (Desig = 'M1') Or (Desig = 'M?')   Then Begin Comp.X := MMsToCoord(103.0); Comp.Y := MMsToCoord(32.0); Comp.Rotation := 0; Inc(Count); End;
        If (Desig = 'IR1') Or (Desig = '*')   Then Begin Comp.X := MMsToCoord(113.5); Comp.Y := MMsToCoord(6.5);  Comp.Rotation := 0; Inc(Count); End;
        If (Desig = 'B3')   Then Begin Comp.X := MMsToCoord(43.5);  Comp.Y := MMsToCoord(47.5); Comp.Rotation := 0; Inc(Count); End;
        If (Desig = 'R2')   Then Begin Comp.X := MMsToCoord(48.5);  Comp.Y := MMsToCoord(47.5); Comp.Rotation := 0; Inc(Count); End;
        If (Desig = 'C10')  Then Begin Comp.X := MMsToCoord(43.5);  Comp.Y := MMsToCoord(42.5); Comp.Rotation := 0; Inc(Count); End;
        If (Desig = 'B4')   Then Begin Comp.X := MMsToCoord(43.5);  Comp.Y := MMsToCoord(35.5); Comp.Rotation := 0; Inc(Count); End;
        If (Desig = 'R3')   Then Begin Comp.X := MMsToCoord(48.5);  Comp.Y := MMsToCoord(35.5); Comp.Rotation := 0; Inc(Count); End;
        If (Desig = 'C11')  Then Begin Comp.X := MMsToCoord(43.5);  Comp.Y := MMsToCoord(30.5); Comp.Rotation := 0; Inc(Count); End;
        If (Desig = 'B1')   Then Begin Comp.X := MMsToCoord(43.5);  Comp.Y := MMsToCoord(23.5); Comp.Rotation := 0; Inc(Count); End;
        If (Desig = 'R4')   Then Begin Comp.X := MMsToCoord(48.5);  Comp.Y := MMsToCoord(23.5); Comp.Rotation := 0; Inc(Count); End;
        If (Desig = 'C3')   Then Begin Comp.X := MMsToCoord(43.5);  Comp.Y := MMsToCoord(18.5); Comp.Rotation := 0; Inc(Count); End;
        If (Desig = 'L2')   Then Begin Comp.X := MMsToCoord(58.0);  Comp.Y := MMsToCoord(49.0); Comp.Rotation := 0; Inc(Count); End;
        If (Desig = 'R5')   Then Begin Comp.X := MMsToCoord(58.0);  Comp.Y := MMsToCoord(45.5); Comp.Rotation := 0; Inc(Count); End;
        If (Desig = 'B2')   Then Begin Comp.X := MMsToCoord(68.0);  Comp.Y := MMsToCoord(47.0); Comp.Rotation := 0; Inc(Count); End;
        If (Desig = 'Q1')   Then Begin Comp.X := MMsToCoord(68.5);  Comp.Y := MMsToCoord(40.5); Comp.Rotation := 0; Inc(Count); End;
        If (Desig = 'R6')   Then Begin Comp.X := MMsToCoord(68.5);  Comp.Y := MMsToCoord(37.0); Comp.Rotation := 0; Inc(Count); End;

        Comp := Iterator.NextPCBObject;
    End;
    Board.BoardIterator_Destroy(Iterator);
    PCBServer.PostProcess;

    ResetParameters;
    AddStringParameter('Action', 'All');
    RunProcess('PCB:Zoom');

    ShowInfo('Thanh cong! Da sap xep ' + IntToStr(Count) + ' linh kien theo Annotation moi vao vi tri ban rong 125x95mm!');
End;