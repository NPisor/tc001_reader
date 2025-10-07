use rusb::{Context, Direction, RequestType, Recipient, UsbContext};
use std::time::Duration;
use rusb::{DeviceHandle, GlobalContext};

const VENDOR_ID: u16 = 0x0BDA;
const PRODUCT_ID: u16 = 0x5830;
const INTERFACE_NUMBER: u8 = 0;

fn get_configuration_descriptor(handle: &mut DeviceHandle<rusb::Context>) -> rusb::Result<Vec<u8>> {
    let mut config_descriptor = vec![0u8; 512];
    handle.read_control(
        rusb::request_type(Direction::In, RequestType::Standard, Recipient::Device),
        0x06,
        0x0200,
        0x0000,
        &mut config_descriptor,
        Duration::from_secs(1),
    )?;
    println!("Received Configuration Descriptor: {:?}", config_descriptor);
    Ok(config_descriptor)
}

fn send_set_interface_request(handle: &mut DeviceHandle<rusb::Context>) -> rusb::Result<()> {
    handle.write_control(
        rusb::request_type(Direction::Out, RequestType::Standard, Recipient::Interface),
        0x0B,      // SET_INTERFACE
        0x0007,    // bAlternateSetting = 7
        0x0001,    // Interface = 1
        &[],       // No payload
        Duration::from_secs(1),
    )?;

    println!("SET INTERFACE request sent.");
    Ok(())
}


fn send_set_configuration(handle: &mut DeviceHandle<rusb::Context>) -> rusb::Result<()> {
    let set_config_raw: Vec<u8> = vec![
        0x1c, 0x00, 0x90, 0x05, 0x9a, 0xab, 0x83, 0xe2, 0xff, 0xff,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x0d,
        0x00, 0x00, 0x02, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x09,
        0x01, 0x00, 0x00, 0x00, 0x00, 0x00
    ];
    handle.write_control(
        rusb::request_type(Direction::Out, RequestType::Standard, Recipient::Device),
        0x09,
        0x0001,
        0x0000,
        &set_config_raw,
        Duration::from_secs(1),
    )?;
    println!("SET CONFIGURATION request sent.");
    Ok(())
}

fn send_vendor_specific_request(handle: &mut DeviceHandle<rusb::Context>) -> rusb::Result<()> {
    let vendor_request_raw: Vec<u8> = vec![
        0x05, 0x84, 0x00, 0x00, 0x00, 0x00, 0x00, 0x08
    ];
    handle.write_control(
        rusb::request_type(Direction::Out, RequestType::Vendor, Recipient::Interface),
        0x45,
        0x0078,
        0x1d00,
        &vendor_request_raw,
        Duration::from_secs(1),
    )?;
    println!("Vendor-specific control request sent.");
    Ok(())
}

fn send_set_cur_request(handle: &mut DeviceHandle<rusb::Context>) -> rusb::Result<()> {
    let set_cur_raw: Vec<u8> = vec![
        0x01, 0x00, 0x01, 0x02, 0x80, 0x1a, 0x06, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x20, 0x00, 0x00, 0x80, 0x01,
        0x00, 0x00, 0x0c, 0x00, 0x00
    ];
    handle.write_control(
        rusb::request_type(Direction::Out, RequestType::Class, Recipient::Interface),
        0x01,
        0x0100, // First SET_CUR: Control selector 1
        0x0001, // Interface 1
        &set_cur_raw,
        Duration::from_secs(1),
    )?;
    println!("SET CUR (first) request sent.");
    Ok(())
}

fn send_set_cur_commit_request(handle: &mut DeviceHandle<rusb::Context>) -> rusb::Result<()> {
    let set_cur_commit_raw: Vec<u8> = vec![
        0x01, 0x00,
        0x01,
        0x02,
        0x80, 0x1a, 0x06, 0x00,
        0x00, 0x00,
        0x00, 0x00,
        0x00, 0x00,
        0x00, 0x00,
        0x20, 0x00,
        0x00, 0x00, 0x03, 0x00, // Corrected!
        0x0c, 0x00, 0x00,
    ];

    // Write second SET_CUR (commit)
    handle.write_control(
        rusb::request_type(Direction::Out, RequestType::Class, Recipient::Interface),
        0x01,   // SET_CUR
        0x0200, // wValue: ControlSelector 2 (Commit)
        0x0001, // wIndex: Interface 1
        &set_cur_commit_raw,
        Duration::from_secs(1),
    )?;

    println!("SET CUR (second / commit) request sent.");

    Ok(())
}

fn read_frames(handle: &mut DeviceHandle<rusb::Context>) -> rusb::Result<()> {
    let mut frame_buffer = vec![0u8; 4096];
    let endpoint = 0x81; // IN endpoint 1
    println!("Starting to read frames...");

    loop {
        match handle.read_interrupt(
            endpoint,
            &mut frame_buffer,
            Duration::from_secs(1),
        ) {
            Ok(size) => {
                println!("Received frame packet of {} bytes", size);
                println!("Frame header (first 16 bytes): {:02X?}", &frame_buffer[..16.min(size)]);
            }
            Err(rusb::Error::Timeout) => {
                continue; // harmless timeout, just keep trying
            }
            Err(e) => {
                eprintln!("Error reading frame: {:?}", e);
                return Err(e); // <-- ✅ return the error instead of break
            }
        }
    }
}







fn main() -> rusb::Result<()> {
    let context = rusb::Context::new()?;

    let device = context.devices()?
        .iter()
        .find(|d| {
            let desc = d.device_descriptor().unwrap();
            desc.vendor_id() == VENDOR_ID && desc.product_id() == PRODUCT_ID
        })
        .expect("Thermal Camera not found!");

    let mut handle = device.open()?;
    handle.claim_interface(INTERFACE_NUMBER)?;

    println!("Device claimed. Sending GET CONFIGURATION DESCRIPTOR...");

    get_configuration_descriptor(&mut handle)?;
    send_set_configuration(&mut handle)?;
    send_vendor_specific_request(&mut handle)?;
    send_set_cur_request(&mut handle)?;
    send_set_cur_commit_request(&mut handle)?;
    send_set_interface_request(&mut handle)?;

    read_frames(&mut handle)?;

    Ok(())
}
